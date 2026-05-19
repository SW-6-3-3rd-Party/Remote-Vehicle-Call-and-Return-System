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
 * CAN0 Node0
 * Classical CAN
 * Standard ID
 * 500kbps
 *
 * RX ID: 0x100
 * TX는 같은 CAN0 Node0에서 CanComm_SendDriveStatus()가 사용한다.
 * ======================================================
 */
#define ACT_CAN_BAUDRATE                    (500000U)
#define ACT_CAN_ISR_PRIORITY_RX             (10U)

/*
 * 100us 단위 tick 기준
 * 200ms = 2000 * 100us
 *
 * alive_counter가 200ms 동안 증가하지 않으면 Fail-Safe 진입.
 * CAN 메시지 자체가 안 와도 alive_counter가 증가하지 않으므로 동일하게 Fail-Safe.
 */
#define ACT_CAN_ALIVE_TIMEOUT_100US_TICKS   (2000U)

#define ACT_CAN_RX_BUFFER_ID                IfxCan_RxBufferId_0
#define ACT_CAN_MODULE_RAM_BASE             (0xF0200000U)

#define ACT_CAN_STB_PORT                    (&MODULE_P20)
#define ACT_CAN_STB_PIN                     (6U)

#define ACT_CAN_TX_BUFFER_COUNT             (2U)

/*
 * CRC-8/SAE-J1850
 * Polynomial = 0x1D
 * Initial    = 0xFF
 * XorOut     = 0xFF
 * No reflection
 */
#define ACT_CAN_CRC8_POLY                   (0x1DU)
#define ACT_CAN_CRC8_INIT                   (0xFFU)
#define ACT_CAN_CRC8_XOROUT                 (0xFFU)
#define ACT_CAN_CRC8_DATA_LEN               (7U)

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
static volatile uint8 g_rxControlModeRaw = ACT_CAN_CONTROL_STANDBY;
static volatile uint8 g_rxSafetyOverrideRaw = ACT_CAN_SAFETY_NORMAL;
static volatile uint8 g_rxAliveCounterRaw = 0U;

static volatile boolean g_rxUpdated = FALSE;

static volatile uint32 g_rxCount = 0U;
static volatile uint32 g_invalidCount = 0U;
static volatile uint32 g_crcErrorCount = 0U;
static volatile uint32 g_aliveErrorCount = 0U;

static uint8 g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
static uint8 g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
static ActGearState g_currentGearState = ACT_GEAR_STATE_P;
static ActControlMode g_currentControlMode = ACT_CONTROL_MODE_STANDBY;
static ActSafetyOverride g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_NORMAL;
static uint8 g_currentAliveCounter = 0U;

static boolean g_aliveCounterInitialized = FALSE;
static uint8 g_lastAliveCounter = 0U;
static volatile uint32 g_aliveTimeoutTick100us = ACT_CAN_ALIVE_TIMEOUT_100US_TICKS;

IFX_INTERRUPT(ActCan_RxIsrHandler, 0, ACT_CAN_ISR_PRIORITY_RX);

static void ActCan_EnableTransceiver(void)
{
    IfxPort_setPinModeOutput(ACT_CAN_STB_PORT,
                             ACT_CAN_STB_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    /*
     * CAN transceiver normal mode
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

static uint8 ActCan_CalcCrc8(const uint8* data, uint32 length)
{
    uint32 i;
    uint8 bit;
    uint8 crc;

    crc = ACT_CAN_CRC8_INIT;

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8)((crc << 1U) ^ ACT_CAN_CRC8_POLY);
            }
            else
            {
                crc = (uint8)(crc << 1U);
            }
        }
    }

    crc ^= ACT_CAN_CRC8_XOROUT;

    return crc;
}

static boolean ActCan_IsAliveCounterUpdated(uint8 aliveCounter)
{
    /*
     * 첫 정상 프레임은 기준값으로 사용한다.
     * 이후부터는 이전 alive_counter와 달라야 MAIN ECU가 살아있다고 본다.
     *
     * 255 -> 0 순환은 값이 달라지므로 정상 처리된다.
     * 중간 프레임 유실로 10 -> 12처럼 증가해도 MAIN이 살아있다고 판단한다.
     */
    if (g_aliveCounterInitialized == FALSE)
    {
        g_aliveCounterInitialized = TRUE;
        g_lastAliveCounter = aliveCounter;
        return TRUE;
    }

    if (aliveCounter == g_lastAliveCounter)
    {
        return FALSE;
    }

    g_lastAliveCounter = aliveCounter;
    return TRUE;
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

    /*
     * brake_key는 운전자의 의도적 제동.
     * safety_override와는 별도 개념이다.
     */
    if (brakeRaw == ACT_CAN_BRAKE_ON)
    {
        MotorControl_Brake();
        return;
    }

    /*
     * accel OFF이면 모터 출력 OFF, 브레이크 해제.
     */
    if (accelRaw == ACT_CAN_ACCEL_OFF)
    {
        MotorControl_Coast();
        return;
    }

    /*
     * accel ON일 때만 gear 상태에 따라 구동.
     */
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
            /*
             * P/N에서는 accel을 눌러도 구동하지 않음.
             */
            MotorControl_Coast();
            break;
    }
}

static void ActCan_EnterSafeState(void)
{
    /*
     * Fail-Safe:
     * - safety_override == 1
     * - alive_counter 200ms 미증가
     * - CAN 미수신
     *
     * 즉시 모터 정지 + 서보 중립.
     */
    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_ON;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_currentControlMode = ACT_CONTROL_MODE_STANDBY;
    g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_FORCE_STOP;

    MotorControl_Brake();
    Steering_Center();
}

static void ActCan_EnterModeInhibitState(uint8 controlModeRaw)
{
    /*
     * Standby / Diagnostic:
     * throttle, brake, gear, steering 제어 입력을 전부 무시한다.
     *
     * - Standby: 시동 전 상태이므로 구동 금지
     * - Diagnostic: 진단 중 정차 상태이므로 구동 금지
     */
    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_currentControlMode = (ActControlMode)controlModeRaw;
    g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_NORMAL;

    MotorControl_Brake();
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
    uint8 aliveCounterRaw;
    uint8 receivedCrc;
    uint8 calculatedCrc;

    IfxCan_Node_clearInterruptFlag(g_actCan.canNode.node,
                                   IfxCan_Interrupt_messageStoredToDedicatedRxBuffer);

    IfxCan_Can_initMessage(&g_actCan.rxMsg);
    g_actCan.rxMsg.bufferNumber = ACT_CAN_RX_BUFFER_ID;

    IfxCan_Can_readMessage(&g_actCan.canNode,
                           &g_actCan.rxMsg,
                           &g_actCan.rxData[0]);

    /*
     * Dedicated RX buffer unlock
     */
    IfxCan_Node_clearRxBufferNewDataFlag(g_actCan.canNode.node,
                                         ACT_CAN_RX_BUFFER_ID);

    if ((g_actCan.rxMsg.messageId != ACT_CAN_CMD_ID) ||
        (g_actCan.rxMsg.dataLengthCode != IfxCan_DataLengthCode_8))
    {
        g_invalidCount++;
        return;
    }

    /*
     * rxData[0] = Byte0~Byte3
     * rxData[1] = Byte4~Byte7
     */
    rxByte[0] = (uint8)((g_actCan.rxData[0] >> 0U)  & 0xFFU);
    rxByte[1] = (uint8)((g_actCan.rxData[0] >> 8U)  & 0xFFU);
    rxByte[2] = (uint8)((g_actCan.rxData[0] >> 16U) & 0xFFU);
    rxByte[3] = (uint8)((g_actCan.rxData[0] >> 24U) & 0xFFU);

    rxByte[4] = (uint8)((g_actCan.rxData[1] >> 0U)  & 0xFFU);
    rxByte[5] = (uint8)((g_actCan.rxData[1] >> 8U)  & 0xFFU);
    rxByte[6] = (uint8)((g_actCan.rxData[1] >> 16U) & 0xFFU);
    rxByte[7] = (uint8)((g_actCan.rxData[1] >> 24U) & 0xFFU);

    /*
     * 1. CRC8 검증
     * Byte0~Byte6으로 계산한 CRC가 Byte7과 다르면 폐기.
     */
    receivedCrc = rxByte[7];
    calculatedCrc = ActCan_CalcCrc8(rxByte, ACT_CAN_CRC8_DATA_LEN);

    if (calculatedCrc != receivedCrc)
    {
        g_crcErrorCount++;
        g_invalidCount++;
        return;
    }

    accelRaw = rxByte[0];
    steeringRaw = rxByte[1];
    brakeRaw = rxByte[2];
    gearRaw = rxByte[3];
    controlModeRaw = rxByte[4];
    safetyOverrideRaw = rxByte[5];
    aliveCounterRaw = rxByte[6];

    /*
     * Payload 값 범위 검증.
     * 범위 밖이면 잘못된 Application Data로 보고 폐기.
     */
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
     * 2. alive_counter 검증
     * 값이 이전 정상 프레임과 같으면 MAIN ECU 메인 루프 정지로 판단하고 폐기.
     *
     * 여기서 바로 Safe-State로 들어가지는 않고,
     * 200ms 동안 alive_counter 증가가 없으면 ActCan_Update100us()에서 Safe-State로 들어간다.
     */
    if (ActCan_IsAliveCounterUpdated(aliveCounterRaw) == FALSE)
    {
        g_aliveErrorCount++;
        g_invalidCount++;
        return;
    }

    /*
     * alive_counter가 증가한 정상 프레임이므로 timeout timer reset.
     */
    g_aliveTimeoutTick100us = 0U;

    g_rxAccelRaw = accelRaw;
    g_rxSteeringRaw = steeringRaw;
    g_rxBrakeRaw = brakeRaw;
    g_rxGearRaw = gearRaw;
    g_rxControlModeRaw = controlModeRaw;
    g_rxSafetyOverrideRaw = safetyOverrideRaw;
    g_rxAliveCounterRaw = aliveCounterRaw;

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

    /*
     * CAN0 Message RAM layout
     */
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
     * ACT ECU는 0x100 명령만 수신한다.
     */
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
    g_rxControlModeRaw = ACT_CAN_CONTROL_STANDBY;
    g_rxSafetyOverrideRaw = ACT_CAN_SAFETY_NORMAL;
    g_rxAliveCounterRaw = 0U;

    g_rxUpdated = FALSE;

    g_rxCount = 0U;
    g_invalidCount = 0U;
    g_crcErrorCount = 0U;
    g_aliveErrorCount = 0U;

    g_currentAccelRaw = ACT_CAN_ACCEL_OFF;
    g_currentBrakeRaw = ACT_CAN_BRAKE_OFF;
    g_currentGearState = ACT_GEAR_STATE_P;
    g_currentControlMode = ACT_CONTROL_MODE_STANDBY;
    g_currentSafetyOverride = ACT_SAFETY_OVERRIDE_NORMAL;
    g_currentAliveCounter = 0U;

    g_aliveCounterInitialized = FALSE;
    g_lastAliveCounter = 0U;
    g_aliveTimeoutTick100us = ACT_CAN_ALIVE_TIMEOUT_100US_TICKS;

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
    uint8 aliveCounterRaw;

    /*
     * alive_counter timeout 감시.
     * 정상 프레임이 들어와서 alive_counter가 증가하면 ISR에서 0으로 reset된다.
     */
    if (g_aliveTimeoutTick100us < ACT_CAN_ALIVE_TIMEOUT_100US_TICKS)
    {
        g_aliveTimeoutTick100us++;
    }

    /*
     * 200ms 동안 alive_counter 증가 없음:
     * - MAIN ECU 메인 루프 정지
     * - CAN 메시지 미수신
     * - CRC 오류 프레임만 계속 수신
     * - alive_counter 동일 프레임만 계속 수신
     *
     * 모두 Fail-Safe.
     */
    if (g_aliveTimeoutTick100us >= ACT_CAN_ALIVE_TIMEOUT_100US_TICKS)
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
        controlModeRaw = g_rxControlModeRaw;
        safetyOverrideRaw = g_rxSafetyOverrideRaw;
        aliveCounterRaw = g_rxAliveCounterRaw;

        g_rxUpdated = FALSE;

        g_currentControlMode = (ActControlMode)controlModeRaw;
        g_currentSafetyOverride = (ActSafetyOverride)safetyOverrideRaw;
        g_currentAliveCounter = aliveCounterRaw;

        /*
         * 3. safety_override 우선 처리
         * safety_override == 1이면 throttle, brake, gear, steering, control_mode 전부 무시.
         */
        if (safetyOverrideRaw == ACT_CAN_SAFETY_FORCE_STOP)
        {
            ActCan_EnterSafeState();
            return;
        }

        /*
         * 4. control_mode 확인
         * Remote Drive가 아니면 모든 제어 신호를 무시하고 정차 상태 유지.
         */
        if (controlModeRaw != ACT_CAN_CONTROL_REMOTE_DRIVE)
        {
            ActCan_EnterModeInhibitState(controlModeRaw);
            return;
        }

        /*
         * 5. 정상 제어 적용
         * Remote Drive + safety_override Normal일 때만 조향/구동 적용.
         */
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

uint32 ActCan_GetCrcErrorCount(void)
{
    return g_crcErrorCount;
}

uint32 ActCan_GetAliveErrorCount(void)
{
    return g_aliveErrorCount;
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
    return g_currentAliveCounter;
}

IfxCan_Can_Node* ActCan_GetCanNode(void)
{
    return &g_actCan.canNode;
}
