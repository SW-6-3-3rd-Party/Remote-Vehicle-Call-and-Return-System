#include "ActCan.h"

#include "Ifx_Types.h"
#include "IfxCan_Can.h"
#include "IfxCan.h"
#include "IfxCpu_Irq.h"
#include "IfxSrc.h"
#include "IfxPort.h"

#include "Motor.h"
#include "Servo.h"
#include "UdsDiag.h"

/*
 * ======================================================
 * TC375 ACT CAN RX
 * ======================================================
 * CAN0 Node0
 * CAN FD with bitrate switching
 * Standard ID
 * Nominal phase: 500 kbit/s
 * Data phase   : 2 Mbit/s
 *
 * RX ID 1: 0x100 MAIN -> ACT command, period 20ms, DLC 6
 * RX ID 2: 0x700 MAIN/Tester -> ACT UDS diagnostic request, DLC 8
 *
 * TX ID 1: 0x200 ACT -> MAIN status, period 50ms, DLC 5
 * TX ID 2: 0x708 ACT -> MAIN/Tester UDS diagnostic response, DLC 8 or 12
 *
 * TX uses the same CAN0 Node0 through ActCan_GetCanNode().
 * ======================================================
 */
#define ACT_CAN_BAUDRATE                    (500000U)
#define ACT_CAN_FAST_BAUDRATE               (2000000U)
#define ACT_CAN_ISR_PRIORITY_RX             (10U)

/*
 * 100us tick 기준.
 * MAIN -> ACT command 주기는 20ms이므로 200ms는 약 10-frame timeout이다.
 */
#define ACT_CAN_CMD_TIMEOUT_100US_TICKS     (2000U)

#define ACT_CAN_RX_BUFFER_ID                IfxCan_RxBufferId_0
#define ACT_CAN_MODULE_RAM_BASE             (0xF0200000U)

#define ACT_CAN_STB_PORT                    (&MODULE_P20)
#define ACT_CAN_STB_PIN                     (6U)

#define ACT_DRIVE_BASE_DUTY_PERCENT         (90U)
#define ACT_TURN_INNER_DUTY_PERCENT         (30U)
#define ACT_TURN_OUTER_DUTY_PERCENT         (100U)

/*
 * TX Buffer 사용 분리:
 *   Status CAN : buffer 0, 1
 *   UDS CAN    : buffer 2, 3
 */
#define ACT_CAN_TX_BUFFER_COUNT             (4U)

IFX_CONST IfxCan_Can_Pins g_actCanPins =
{
    &IfxCan_TXD00_P20_8_OUT,
    IfxPort_OutputMode_pushPull,

    &IfxCan_RXD00B_P20_7_IN,
    IfxPort_InputMode_pullUp,

    IfxPort_PadDriver_cmosAutomotiveSpeed4
};

typedef struct
{
    IfxCan_Can_Config canConfig;
    IfxCan_Can canModule;
    IfxCan_Can_Node canNode;
    IfxCan_Can_NodeConfig nodeConfig;

    IfxCan_Filter canFilterCmd;
    IfxCan_Filter canFilterDiag;

    IfxCan_Message rxMsg;
    uint32 rxData[2];
} ActCanDriver;

static ActCanDriver g_actCan;

static volatile uint8 g_rxAccelRaw = ACT_CAN_ACCEL_OFF;
static volatile uint8 g_rxSteeringRaw = ACT_CAN_STEERING_NULL;
static volatile uint8 g_rxBrakeRaw = ACT_CAN_BRAKE_OFF;
static volatile uint8 g_rxGearRaw = ACT_CAN_GEAR_P;
static volatile uint8 g_rxControlModeRaw = ACT_CAN_CONTROL_STANDBY;
static volatile uint8 g_rxSafetyOverrideRaw = ACT_CAN_SAFETY_NORMAL;

static volatile boolean g_rxUpdated = FALSE;

static volatile uint32 g_rxCount = 0U;
static volatile uint32 g_invalidCount = 0U;
static volatile uint32 g_timeoutCount = 0U;

static uint8 g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
static uint8 g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
static ActGearState g_currentGearState = ACT_GEAR_STATE_P;
static ActControlMode g_currentControlMode = ACT_CONTROL_MODE_STANDBY;
static ActSafetyOverride g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_NORMAL;

static volatile uint32 g_cmdTimeoutTick100us = ACT_CAN_CMD_TIMEOUT_100US_TICKS;
static boolean g_safeStateEnteredByTimeout = FALSE;

IFX_INTERRUPT(ActCan_RxIsrHandler, 0, ACT_CAN_ISR_PRIORITY_RX);

static void ActCan_EnableTransceiver(void)
{
    IfxPort_setPinModeOutput(ACT_CAN_STB_PORT,
                             ACT_CAN_STB_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    /*
     * CAN transceiver normal mode:
     * STB LOW = enable
     */
    IfxPort_setPinLow(ACT_CAN_STB_PORT, ACT_CAN_STB_PIN);
}

static boolean ActCan_IsValidBool(uint8 value)
{
    return ((value == 0U) || (value == 1U));
}

static boolean ActCan_IsValidSteering(uint8 value)
{
    return ((value == ACT_CAN_STEERING_NULL) ||
            (value == ACT_CAN_STEERING_LEFT) ||
            (value == ACT_CAN_STEERING_RIGHT));
}

static boolean ActCan_IsValidGear(uint8 value)
{
    return ((value == ACT_CAN_GEAR_P) ||
            (value == ACT_CAN_GEAR_R) ||
            (value == ACT_CAN_GEAR_N) ||
            (value == ACT_CAN_GEAR_D));
}

static boolean ActCan_IsValidControlMode(uint8 value)
{
    return ((value == ACT_CAN_CONTROL_STANDBY) ||
            (value == ACT_CAN_CONTROL_REMOTE_DRIVE) ||
            (value == ACT_CAN_CONTROL_DIAGNOSTIC));
}

static boolean ActCan_IsValidSafetyOverride(uint8 value)
{
    return ((value == ACT_CAN_SAFETY_NORMAL) ||
            (value == ACT_CAN_SAFETY_FORCE_STOP));
}

static boolean ActCan_IsCanFdFrameMode(IfxCan_FrameMode frameMode)
{
    return ((frameMode == IfxCan_FrameMode_fdLong) ||
            (frameMode == IfxCan_FrameMode_fdLongAndFast));
}

static void ActCan_ApplySteering(uint8 steeringRaw)
{
    switch (steeringRaw)
    {
        case ACT_CAN_STEERING_LEFT:
            Steering_SetKey(STEERING_KEY_LEFT);
            MotorControl_SetWheelDutyPercent(ACT_TURN_INNER_DUTY_PERCENT,
                                             ACT_TURN_OUTER_DUTY_PERCENT);
            break;

        case ACT_CAN_STEERING_RIGHT:
            Steering_SetKey(STEERING_KEY_RIGHT);
            MotorControl_SetWheelDutyPercent(ACT_TURN_OUTER_DUTY_PERCENT,
                                             ACT_TURN_INNER_DUTY_PERCENT);
            break;

        case ACT_CAN_STEERING_NULL:
        default:
            Steering_SetKey(STEERING_KEY_NULL);
            MotorControl_SetDutyPercent(ACT_DRIVE_BASE_DUTY_PERCENT);
            break;
    }
}

static void ActCan_ApplyDrive(uint8 accelRaw, uint8 brakeRaw, uint8 gearRaw)
{
    g_currentAccelRaw = accelRaw;
    g_currentBrakeRaw = brakeRaw;
    g_currentGearState = (ActGearState)gearRaw;

    if (brakeRaw == ACT_CAN_BRAKE_ON)
    {
        MotorControl_Brake();
        return;
    }

    if (accelRaw == ACT_CAN_ACCEL_OFF)
    {
        MotorControl_Coast();
        return;
    }

    switch (gearRaw)
    {
        case ACT_CAN_GEAR_D:
            MotorControl_Forward();
            break;

        case ACT_CAN_GEAR_R:
            MotorControl_Reverse();
            break;

        case ACT_CAN_GEAR_P:
        case ACT_CAN_GEAR_N:
        default:
            MotorControl_Coast();
            break;
    }
}

static void ActCan_EnterSafeState(void)
{
    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_ON;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_currentControlMode = ACT_CONTROL_MODE_STANDBY;
    g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_FORCE_STOP;

    MotorControl_Brake();
    MotorControl_SetDutyPercent(ACT_DRIVE_BASE_DUTY_PERCENT);
    Steering_Center();
}

static void ActCan_EnterModeInhibitState(uint8 controlModeRaw)
{
    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_currentControlMode = (ActControlMode)controlModeRaw;
    g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_NORMAL;

    MotorControl_Brake();
    MotorControl_SetDutyPercent(ACT_DRIVE_BASE_DUTY_PERCENT);
    Steering_Center();
}

void ActCan_RxIsrHandler(void)
{
    uint8 rxByte[8];

    uint8 accelRaw;
    uint8 steeringRaw;
    uint8 brakeRaw;
    uint8 gearRaw;
    uint8 controlModeRaw;
    uint8 safetyOverrideRaw;

    IfxCan_Node_clearInterruptFlag(g_actCan.canNode.node,
                                   IfxCan_Interrupt_messageStoredToDedicatedRxBuffer);

    IfxCan_Can_initMessage(&g_actCan.rxMsg);
    g_actCan.rxMsg.bufferNumber = ACT_CAN_RX_BUFFER_ID;

    IfxCan_Can_readMessage(&g_actCan.canNode,
                           &g_actCan.rxMsg,
                           &g_actCan.rxData[0]);

    IfxCan_Node_clearRxBufferNewDataFlag(g_actCan.canNode.node,
                                         ACT_CAN_RX_BUFFER_ID);

    rxByte[0] = (uint8)((g_actCan.rxData[0] >> 0U)  & 0xFFU);
    rxByte[1] = (uint8)((g_actCan.rxData[0] >> 8U)  & 0xFFU);
    rxByte[2] = (uint8)((g_actCan.rxData[0] >> 16U) & 0xFFU);
    rxByte[3] = (uint8)((g_actCan.rxData[0] >> 24U) & 0xFFU);

    rxByte[4] = (uint8)((g_actCan.rxData[1] >> 0U)  & 0xFFU);
    rxByte[5] = (uint8)((g_actCan.rxData[1] >> 8U)  & 0xFFU);
    rxByte[6] = (uint8)((g_actCan.rxData[1] >> 16U) & 0xFFU);
    rxByte[7] = (uint8)((g_actCan.rxData[1] >> 24U) & 0xFFU);

    /*
     * ======================================================
     * UDS Diagnostic Request
     * MAIN/Tester -> ACT
     * CAN ID 0x700
     *
     * ISR에서는 요청만 저장하고,
     * 실제 서비스 처리는 UdsDiag_Update1ms()에서 수행한다.
     * ======================================================
     */
    if (g_actCan.rxMsg.messageId == UDS_DIAG_REQ_CAN_ID)
    {
        if ((ActCan_IsCanFdFrameMode(g_actCan.rxMsg.frameMode) == TRUE) &&
            (g_actCan.rxMsg.dataLengthCode == IfxCan_DataLengthCode_8))
        {
            UdsDiag_OnCanRequest(rxByte);
        }
        else
        {
            g_invalidCount++;
        }

        return;
    }

    /*
     * ======================================================
     * Normal ACT Control Command
     * MAIN -> ACT
     * CAN ID 0x100
     * DLC 6
     * ======================================================
     */
    if ((g_actCan.rxMsg.messageId != ACT_CAN_CMD_ID) ||
        (ActCan_IsCanFdFrameMode(g_actCan.rxMsg.frameMode) == FALSE) ||
        (g_actCan.rxMsg.dataLengthCode != ACT_CAN_DLC_IFX))
    {
        g_invalidCount++;
        return;
    }

    accelRaw = rxByte[0];
    steeringRaw = rxByte[1];
    brakeRaw = rxByte[2];
    gearRaw = rxByte[3];
    controlModeRaw = rxByte[4];
    safetyOverrideRaw = rxByte[5];

    if ((ActCan_IsValidBool(accelRaw) == FALSE) ||
        (ActCan_IsValidSteering(steeringRaw) == FALSE) ||
        (ActCan_IsValidBool(brakeRaw) == FALSE) ||
        (ActCan_IsValidGear(gearRaw) == FALSE) ||
        (ActCan_IsValidControlMode(controlModeRaw) == FALSE) ||
        (ActCan_IsValidSafetyOverride(safetyOverrideRaw) == FALSE))
    {
        g_invalidCount++;
        return;
    }

    /*
     * Valid command frame received.
     * Reset command timeout.
     */
    g_cmdTimeoutTick100us = 0U;
    g_safeStateEnteredByTimeout = FALSE;

    g_rxAccelRaw = accelRaw;
    g_rxSteeringRaw = steeringRaw;
    g_rxBrakeRaw = brakeRaw;
    g_rxGearRaw = gearRaw;
    g_rxControlModeRaw = controlModeRaw;
    g_rxSafetyOverrideRaw = safetyOverrideRaw;

    g_rxUpdated = TRUE;
    g_rxCount++;
}

void ActCan_Init(void)
{
    ActCan_EnableTransceiver();

    IfxCan_Can_initModuleConfig(&g_actCan.canConfig, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_actCan.canModule, &g_actCan.canConfig);

    IfxCan_Can_initNodeConfig(&g_actCan.nodeConfig, &g_actCan.canModule);

    g_actCan.nodeConfig.busLoopbackEnabled = FALSE;
    g_actCan.nodeConfig.nodeId = IfxCan_NodeId_0;
    g_actCan.nodeConfig.clockSource = IfxCan_ClockSource_both;
    g_actCan.nodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    g_actCan.nodeConfig.frame.mode = IfxCan_FrameMode_fdLongAndFast;
    g_actCan.nodeConfig.baudRate.baudrate = ACT_CAN_BAUDRATE;
    g_actCan.nodeConfig.fastBaudRate.baudrate = ACT_CAN_FAST_BAUDRATE;

    g_actCan.nodeConfig.txConfig.txMode = IfxCan_TxMode_dedicatedBuffers;
    g_actCan.nodeConfig.txConfig.dedicatedTxBuffersNumber = ACT_CAN_TX_BUFFER_COUNT;
    g_actCan.nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_12;

    g_actCan.nodeConfig.rxConfig.rxMode = IfxCan_RxMode_dedicatedBuffers;
    g_actCan.nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_8;

    /*
     * Standard Filter 2개:
     *   Filter 0: 0x100 control command
     *   Filter 1: 0x700 UDS diagnostic request
     */
    g_actCan.nodeConfig.filterConfig.messageIdLength = IfxCan_MessageIdLength_standard;
    g_actCan.nodeConfig.filterConfig.standardListSize = 2U;
    g_actCan.nodeConfig.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;
    g_actCan.nodeConfig.filterConfig.rejectRemoteFramesWithStandardId = TRUE;
    g_actCan.nodeConfig.filterConfig.rejectRemoteFramesWithExtendedId = TRUE;

    g_actCan.nodeConfig.messageRAM.baseAddress = ACT_CAN_MODULE_RAM_BASE;
    g_actCan.nodeConfig.messageRAM.standardFilterListStartAddress = 0x000U;
    g_actCan.nodeConfig.messageRAM.rxBuffersStartAddress = 0x100U;
    g_actCan.nodeConfig.messageRAM.txBuffersStartAddress = 0x200U;

    g_actCan.nodeConfig.interruptConfig.messageStoredToDedicatedRxBufferEnabled = TRUE;
    g_actCan.nodeConfig.interruptConfig.reint.priority = ACT_CAN_ISR_PRIORITY_RX;
    g_actCan.nodeConfig.interruptConfig.reint.interruptLine = IfxCan_InterruptLine_1;
    g_actCan.nodeConfig.interruptConfig.reint.typeOfService = IfxSrc_Tos_cpu0;

    g_actCan.nodeConfig.pins = &g_actCanPins;

    IfxCan_Can_initNode(&g_actCan.canNode, &g_actCan.nodeConfig);

    /*
     * Filter 0:
     * MAIN -> ACT control command
     * CAN ID 0x100
     */
    g_actCan.canFilterCmd.number = 0U;
    g_actCan.canFilterCmd.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxBuffer;
    g_actCan.canFilterCmd.type = IfxCan_FilterType_none;
    g_actCan.canFilterCmd.id1 = ACT_CAN_CMD_ID;
    g_actCan.canFilterCmd.id2 = ACT_CAN_CMD_ID;
    g_actCan.canFilterCmd.rxBufferOffset = ACT_CAN_RX_BUFFER_ID;

    IfxCan_Can_setStandardFilter(&g_actCan.canNode, &g_actCan.canFilterCmd);

    /*
     * Filter 1:
     * MAIN/Tester -> ACT UDS diagnostic request
     * CAN ID 0x700
     *
     * 같은 Dedicated RX Buffer 0에 저장하고,
     * ISR에서 messageId로 0x100 / 0x700을 분기한다.
     */
    g_actCan.canFilterDiag.number = 1U;
    g_actCan.canFilterDiag.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxBuffer;
    g_actCan.canFilterDiag.type = IfxCan_FilterType_none;
    g_actCan.canFilterDiag.id1 = UDS_DIAG_REQ_CAN_ID;
    g_actCan.canFilterDiag.id2 = UDS_DIAG_REQ_CAN_ID;
    g_actCan.canFilterDiag.rxBufferOffset = ACT_CAN_RX_BUFFER_ID;

    IfxCan_Can_setStandardFilter(&g_actCan.canNode, &g_actCan.canFilterDiag);

    g_rxAccelRaw = ACT_CAN_ACCEL_OFF;
    g_rxSteeringRaw = ACT_CAN_STEERING_NULL;
    g_rxBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_rxGearRaw = ACT_CAN_GEAR_P;
    g_rxControlModeRaw = ACT_CAN_CONTROL_STANDBY;
    g_rxSafetyOverrideRaw = ACT_CAN_SAFETY_NORMAL;

    g_rxUpdated = FALSE;

    g_rxCount = 0U;
    g_invalidCount = 0U;
    g_timeoutCount = 0U;

    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_currentControlMode = ACT_CONTROL_MODE_STANDBY;
    g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_NORMAL;

    g_cmdTimeoutTick100us = ACT_CAN_CMD_TIMEOUT_100US_TICKS;
    g_safeStateEnteredByTimeout = FALSE;

    MotorControl_SetDutyPercent(ACT_DRIVE_BASE_DUTY_PERCENT);
    MotorControl_Coast();
    Steering_Center();
}

void ActCan_Update100us(void)
{
    uint8 accelRaw;
    uint8 steeringRaw;
    uint8 brakeRaw;
    uint8 gearRaw;
    uint8 controlModeRaw;
    uint8 safetyOverrideRaw;

    if (g_cmdTimeoutTick100us < ACT_CAN_CMD_TIMEOUT_100US_TICKS)
    {
        g_cmdTimeoutTick100us++;
    }

    if (g_cmdTimeoutTick100us >= ACT_CAN_CMD_TIMEOUT_100US_TICKS)
    {
        if (g_safeStateEnteredByTimeout == FALSE)
        {
            g_timeoutCount++;
            g_safeStateEnteredByTimeout = TRUE;
        }

        ActCan_EnterSafeState();
        return;
    }

    if (g_rxUpdated == TRUE)
    {
        accelRaw = g_rxAccelRaw;
        steeringRaw = g_rxSteeringRaw;
        brakeRaw = g_rxBrakeRaw;
        gearRaw = g_rxGearRaw;
        controlModeRaw = g_rxControlModeRaw;
        safetyOverrideRaw = g_rxSafetyOverrideRaw;

        g_rxUpdated = FALSE;

        g_currentControlMode = (ActControlMode)controlModeRaw;
        g_currentSafetyOverride = (ActSafetyOverride)safetyOverrideRaw;

        if (safetyOverrideRaw == ACT_CAN_SAFETY_FORCE_STOP)
        {
            ActCan_EnterSafeState();
            return;
        }

        if (controlModeRaw != ACT_CAN_CONTROL_REMOTE_DRIVE)
        {
            ActCan_EnterModeInhibitState(controlModeRaw);
            return;
        }

        ActCan_ApplySteering(steeringRaw);
        ActCan_ApplyDrive(accelRaw, brakeRaw, gearRaw);
    }
}

uint32 ActCan_GetRxCount(void)
{
    return g_rxCount;
}

uint32 ActCan_GetInvalidCount(void)
{
    return g_invalidCount;
}

uint32 ActCan_GetTimeoutCount(void)
{
    return g_timeoutCount;
}

uint32 ActCan_GetCrcErrorCount(void)
{
    return 0U;
}

uint32 ActCan_GetAliveErrorCount(void)
{
    return 0U;
}

ActGearState ActCan_GetGearState(void)
{
    return g_currentGearState;
}

ActControlMode ActCan_GetControlMode(void)
{
    return g_currentControlMode;
}

ActSafetyOverride ActCan_GetSafetyOverride(void)
{
    return g_currentSafetyOverride;
}

uint8 ActCan_GetAliveCounter(void)
{
    return 0U;
}

IfxCan_Can_Node* ActCan_GetCanNode(void)
{
    return &g_actCan.canNode;
}
