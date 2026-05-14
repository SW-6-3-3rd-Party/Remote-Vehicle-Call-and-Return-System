#include "ActCan.h"

#include "Ifx_Types.h"
#include "IfxCan_Can.h"
#include "IfxCan.h"
#include "IfxCpu_Irq.h"
#include "IfxSrc.h"
#include "IfxPort.h"

#include "Motor.h"
#include "Servo.h"

/*
 * ======================================================
 * TC375 ACT CAN RX
 * ======================================================
 * 기본값은 CAN0 Node0, 500kbps, Standard ID 0x321 수신이다.
 * 보드/쉴드에서 사용하는 CAN 핀이 다르면 아래 g_actCanPins만 바꾸면 된다.
 */
#define ACT_CAN_BAUDRATE              (500000U)
#define ACT_CAN_ISR_PRIORITY_RX       (10U)
#define ACT_CAN_TIMEOUT_100US_TICKS   (5000U)     /* 500ms */
#define ACT_CAN_RX_BUFFER_ID          IfxCan_RxBufferId_0
#define ACT_CAN_MODULE_RAM_BASE       (0xF0200000U)

#define ACT_CAN_STB_PORT              (&MODULE_P20)
#define ACT_CAN_STB_PIN               (6U)

#define ACT_CAN_TX_BUFFER_COUNT       (2U)

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
    IfxCan_Filter canFilter;
    IfxCan_Message rxMsg;
    uint32 rxData[2];
} ActCanDriver;

static ActCanDriver g_actCan;

static volatile uint8 g_rxAccelRaw = ACT_CAN_ACCEL_OFF;
static volatile uint8 g_rxSteeringRaw = ACT_CAN_STEERING_NULL;
static volatile uint8 g_rxBrakeRaw = ACT_CAN_BRAKE_OFF;
static volatile uint8 g_rxGearRaw = ACT_CAN_GEAR_P;
static volatile boolean g_rxUpdated = FALSE;
static volatile uint32 g_rxCount = 0U;
static volatile uint32 g_invalidCount = 0U;

static uint8 g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
static uint8 g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
static ActGearState g_currentGearState = ACT_GEAR_STATE_P;
static uint32 g_timeoutTick100us = ACT_CAN_TIMEOUT_100US_TICKS;

IFX_INTERRUPT(ActCan_RxIsrHandler, 0, ACT_CAN_ISR_PRIORITY_RX);

static void ActCan_EnableTransceiver(void)
{
    IfxPort_setPinModeOutput(ACT_CAN_STB_PORT,
                             ACT_CAN_STB_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

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

static void ActCan_ApplySteering(uint8 steeringRaw)
{
    switch (steeringRaw)
    {
        case ACT_CAN_STEERING_LEFT:
            Steering_SetKey(STEERING_KEY_LEFT);
            break;

        case ACT_CAN_STEERING_RIGHT:
            Steering_SetKey(STEERING_KEY_RIGHT);
            break;

        case ACT_CAN_STEERING_NULL:
        default:
            Steering_SetKey(STEERING_KEY_NULL);
            break;
    }
}

static void ActCan_ApplyDrive(uint8 accelRaw, uint8 brakeRaw, uint8 gearRaw)
{
    g_currentAccelRaw = accelRaw;
    g_currentBrakeRaw = brakeRaw;
    g_currentGearState = (ActGearState)gearRaw;

    /* 브레이크 키는 눌렀을 때만 실제 브레이크 ON */
    if (brakeRaw == ACT_CAN_BRAKE_ON)
    {
        MotorControl_Brake();
        return;
    }

    /* 브레이크 OFF + 엑셀 OFF: 모터 출력 OFF, 브레이크 OFF */
    if (accelRaw == ACT_CAN_ACCEL_OFF)
    {
        MotorControl_Coast();
        return;
    }

    /* 엑셀 ON일 때만 기어 상태에 따라 구동 */
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
            /* P/N에서는 엑셀을 눌러도 구동하지 않음 */
            MotorControl_Coast();
            break;
    }
}

static void ActCan_EnterSafeState(void)
{
    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_ON;
    g_currentGearState = ACT_GEAR_STATE_P;

    MotorControl_Brake();
    Steering_Center();
}

void ActCan_RxIsrHandler(void)
{
    uint8 accelRaw;
    uint8 steeringRaw;
    uint8 brakeRaw;
    uint8 gearRaw;

    IfxCan_Node_clearInterruptFlag(g_actCan.canNode.node,
                                   IfxCan_Interrupt_messageStoredToDedicatedRxBuffer);

    IfxCan_Can_initMessage(&g_actCan.rxMsg);
    g_actCan.rxMsg.bufferNumber = ACT_CAN_RX_BUFFER_ID;

    IfxCan_Can_readMessage(&g_actCan.canNode,
                           &g_actCan.rxMsg,
                           &g_actCan.rxData[0]);

    /* Dedicated RX buffer unlock */
    IfxCan_Node_clearRxBufferNewDataFlag(g_actCan.canNode.node,
                                         ACT_CAN_RX_BUFFER_ID);

    if ((g_actCan.rxMsg.messageId == ACT_CAN_CMD_ID) &&
        (g_actCan.rxMsg.dataLengthCode == IfxCan_DataLengthCode_8))
    {
        accelRaw    = (uint8)((g_actCan.rxData[0] >> 0U)  & 0xFFU);
        steeringRaw = (uint8)((g_actCan.rxData[0] >> 8U)  & 0xFFU);
        brakeRaw    = (uint8)((g_actCan.rxData[0] >> 16U) & 0xFFU);
        gearRaw     = (uint8)((g_actCan.rxData[0] >> 24U) & 0xFFU);

        if ((ActCan_IsValidBool(accelRaw) == TRUE) &&
            (ActCan_IsValidSteering(steeringRaw) == TRUE) &&
            (ActCan_IsValidBool(brakeRaw) == TRUE) &&
            (ActCan_IsValidGear(gearRaw) == TRUE))
        {
            g_rxAccelRaw = accelRaw;
            g_rxSteeringRaw = steeringRaw;
            g_rxBrakeRaw = brakeRaw;
            g_rxGearRaw = gearRaw;
            g_rxUpdated = TRUE;
            g_rxCount++;
            g_timeoutTick100us = 0U;
        }
        else
        {
            g_invalidCount++;
        }
    }
    else
    {
        g_invalidCount++;
    }
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
    g_actCan.nodeConfig.frame.mode = IfxCan_FrameMode_standard;
    g_actCan.nodeConfig.baudRate.baudrate = ACT_CAN_BAUDRATE;

    /*
     * TX도 같은 CAN0 Node0에서 같이 사용한다.
     * CanComm_SendDriveStatus()가 Dedicated TX Buffer 0/1을 사용하므로
     * 여기서 반드시 TX buffer를 할당해야 한다.
     */
    g_actCan.nodeConfig.txConfig.txMode = IfxCan_TxMode_dedicatedBuffers;
    g_actCan.nodeConfig.txConfig.dedicatedTxBuffersNumber = ACT_CAN_TX_BUFFER_COUNT;
    g_actCan.nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_8;

    g_actCan.nodeConfig.rxConfig.rxMode = IfxCan_RxMode_dedicatedBuffers;
    g_actCan.nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_8;

    g_actCan.nodeConfig.filterConfig.messageIdLength = IfxCan_MessageIdLength_standard;
    g_actCan.nodeConfig.filterConfig.standardListSize = 1U;
    g_actCan.nodeConfig.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;
    g_actCan.nodeConfig.filterConfig.rejectRemoteFramesWithStandardId = TRUE;
    g_actCan.nodeConfig.filterConfig.rejectRemoteFramesWithExtendedId = TRUE;

    /* Message RAM layout for CAN0 Node0 */
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

    g_actCan.canFilter.number = 0U;
    g_actCan.canFilter.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxBuffer;
    g_actCan.canFilter.type = IfxCan_FilterType_none;
    g_actCan.canFilter.id1 = ACT_CAN_CMD_ID;
    g_actCan.canFilter.id2 = ACT_CAN_CMD_ID;
    g_actCan.canFilter.rxBufferOffset = ACT_CAN_RX_BUFFER_ID;

    IfxCan_Can_setStandardFilter(&g_actCan.canNode, &g_actCan.canFilter);

    g_rxAccelRaw = ACT_CAN_ACCEL_OFF;
    g_rxSteeringRaw = ACT_CAN_STEERING_NULL;
    g_rxBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_rxGearRaw = ACT_CAN_GEAR_P;
    g_rxUpdated = FALSE;
    g_rxCount = 0U;
    g_invalidCount = 0U;

    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_timeoutTick100us = ACT_CAN_TIMEOUT_100US_TICKS;

    MotorControl_Coast();
    Steering_Center();
}

void ActCan_Update100us(void)
{
    uint8 accelRaw;
    uint8 steeringRaw;
    uint8 brakeRaw;
    uint8 gearRaw;

    if (g_timeoutTick100us < ACT_CAN_TIMEOUT_100US_TICKS)
    {
        g_timeoutTick100us++;
    }

    if (g_timeoutTick100us >= ACT_CAN_TIMEOUT_100US_TICKS)
    {
        ActCan_EnterSafeState();
        return;
    }

    if (g_rxUpdated == TRUE)
    {
        accelRaw = g_rxAccelRaw;
        steeringRaw = g_rxSteeringRaw;
        brakeRaw = g_rxBrakeRaw;
        gearRaw = g_rxGearRaw;
        g_rxUpdated = FALSE;

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

ActGearState ActCan_GetGearState(void)
{
    return g_currentGearState;
}

IfxCan_Can_Node* ActCan_GetCanNode(void)
{
    return &g_actCan.canNode;
}
