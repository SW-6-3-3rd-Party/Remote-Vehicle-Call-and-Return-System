#include "BodyCan.h"

#include "Ifx_Types.h"
#include "IfxCan_Can.h"
#include "IfxCan.h"
#include "IfxCpu_Irq.h"
#include "IfxSrc.h"
#include "IfxPort.h"

#include "BodyControl.h"

/*
 * ======================================================
 * TC375 BODY CAN
 * ======================================================
 * CAN Bus 2 protocol
 * CAN0 Node0 physical interface
 * CAN FD
 * Standard ID
 * 500kbps nominal, no bit-rate switching
 *
 * RX ID : 0x110 BODY control command
 * RX ID : 0x710 UDS request, CanTp Single Frame
 * TX ID : 0x210 collision event
 * TX ID : 0x310 heartbeat
 * TX ID : 0x718 UDS response, CanTp Single Frame
 *
 * AURIX TC375 Lite Kit CANH/CANL connector near Ethernet
 * TX  = P20.8 = IfxCan_TXD00_P20_8_OUT
 * RX  = P20.7 = IfxCan_RXD00B_P20_7_IN
 * STB = P20.6 = LOW to enable CAN transceiver
 * ======================================================
 */

#define BODY_CAN_BAUDRATE                    (500000U)
#define BODY_CAN_FD_DATA_BAUDRATE            (500000U)
#define BODY_CAN_FRAME_MODE                  IfxCan_FrameMode_fdLong
#define BODY_CAN_ISR_PRIORITY_RX             (10U)

#define BODY_CAN_RX_BUFFER_ID                IfxCan_RxBufferId_0
#define BODY_CAN_MODULE_RAM_BASE             (0xF0200000U)

#define BODY_CAN_STB_PORT                    (&MODULE_P20)
#define BODY_CAN_STB_PIN                     (6U)

#define BODY_CAN_TX_BUFFER_COUNT             (2U)

/*
 * RX command period is 20ms. Enter safe state after 200ms without a valid command.
 */
#define BODY_CAN_CMD_TIMEOUT_MS              (200U)

/*
 * BODY -> MAIN heartbeat 0x310 TX period.
 */
#define BODY_CAN_HEARTBEAT_TX_PERIOD_MS      (200U)

#define BODY_UDS_SF_MAX_PAYLOAD_BYTES        (7U)
#define BODY_UDS_NRC_SERVICE_NOT_SUPPORTED   (0x11U)
#define BODY_UDS_NRC_SUBFUNCTION_UNSUPPORTED (0x12U)
#define BODY_UDS_NRC_INCORRECT_LENGTH        (0x13U)
#define BODY_UDS_NRC_REQUEST_OUT_OF_RANGE    (0x31U)

#define BODY_UDS_DTC_STATUS_AVAILABILITY_MASK (0x01U)
#define BODY_UDS_DTC_STATUS_TEST_FAILED       (0x01U)
#define BODY_UDS_DTC_ULTRASONIC_BYTE0         (0xC2U)
#define BODY_UDS_DTC_ULTRASONIC_BYTE1         (0x01U)
#define BODY_UDS_DTC_ULTRASONIC_BYTE2         (0x00U)

#define BODY_UDS_ROUTINE_START                (0x01U)
#define BODY_UDS_ROUTINE_RESULT_OK            (0x00U)
#define BODY_UDS_ROUTINE_RESULT_TIMEOUT       (0x01U)

IFX_CONST IfxCan_Can_Pins g_bodyCanPins =
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
    IfxCan_Filter canFilter[2];
    IfxCan_Message rxMsg;
    uint32 rxData[2];
} BodyCanDriver;

static BodyCanDriver g_bodyCan;

static volatile uint8 g_rxHeadlampRaw = BODY_CAN_OFF;
static volatile uint8 g_rxTurnSignalRaw = BODY_CAN_TURN_OFF;
static volatile uint8 g_rxBrakeLampRaw = BODY_CAN_OFF;
static volatile uint8 g_rxHornRaw = BODY_CAN_OFF;
static volatile uint8 g_rxCollisionWarningRaw = BODY_CAN_OFF;
static volatile uint8 g_rxControlModeRaw = BODY_CAN_CONTROL_STANDBY;

static volatile boolean g_rxUpdated = FALSE;

static volatile uint32 g_rxCount = 0U;
static volatile uint32 g_invalidCount = 0U;
static volatile uint32 g_crcErrorCount = 0U;
static volatile uint32 g_aliveErrorCount = 0U;

static uint8 g_currentHeadlampRaw = BODY_CAN_OFF;
static uint8 g_currentTurnSignalRaw = BODY_CAN_TURN_OFF;
static uint8 g_currentBrakeLampRaw = BODY_CAN_OFF;
static uint8 g_currentHornRaw = BODY_CAN_OFF;
static uint8 g_currentCollisionWarningRaw = BODY_CAN_OFF;
static uint8 g_currentControlModeRaw = BODY_CAN_CONTROL_STANDBY;
static uint8 g_currentRxAliveCounter = 0U;

static volatile uint32 g_cmdTimeoutMs = BODY_CAN_CMD_TIMEOUT_MS;

static uint8 g_txBufferIndex = 0U;
static uint8 g_txHeartbeatAliveCounter = 0U;
static uint32 g_heartbeatTxTimerMs = 0U;
static uint32 g_bodyCanTimeMs = 0U;

static boolean g_collisionEventPending = FALSE;
static uint32 g_pendingCollisionOccurrenceTimeMs = 0U;

static volatile boolean g_udsRequestUpdated = FALSE;
static volatile uint8 g_udsRequestByte[8] = {0U};
static uint8 g_udsSession = BODY_UDS_SESSION_DEFAULT;
static volatile uint32 g_udsRxCount = 0U;
static volatile uint32 g_udsTxCount = 0U;
static volatile uint32 g_udsTxBusyCount = 0U;
static volatile uint8 g_udsLastSid = 0U;
static volatile uint8 g_udsLastNrc = 0U;

volatile uint32 g_debugBodyCanRxCount = 0U;
volatile uint32 g_debugBodyCanInvalidCount = 0U;
volatile uint32 g_debugBodyCanCrcErrorCount = 0U;
volatile uint32 g_debugBodyCanAliveErrorCount = 0U;

volatile uint32 g_debugBodyCanStatusTxCallCount = 0U;
volatile uint32 g_debugBodyCanStatusTxOkCount = 0U;
volatile uint32 g_debugBodyCanStatusTxBusyCount = 0U;
volatile uint32 g_debugBodyCanEventTxCallCount = 0U;
volatile uint32 g_debugBodyCanEventTxOkCount = 0U;
volatile uint32 g_debugBodyCanEventTxBusyCount = 0U;
volatile uint32 g_debugBodyCanEventPending = 0U;
volatile uint32 g_debugBodyCanLastCollisionTimeMs = 0U;

volatile uint32 g_debugBodyCanUdsRxCount = 0U;
volatile uint32 g_debugBodyCanUdsTxCount = 0U;
volatile uint32 g_debugBodyCanUdsTxBusyCount = 0U;
volatile uint8 g_debugBodyCanUdsSession = BODY_UDS_SESSION_DEFAULT;
volatile uint8 g_debugBodyCanUdsLastSid = 0U;
volatile uint8 g_debugBodyCanUdsLastNrc = 0U;

volatile uint8 g_debugBodyCanRxAliveCounter = 0U;
volatile uint8 g_debugBodyCanTxAliveCounter = 0U;
volatile uint8 g_debugBodyCanLastRxCrc = 0U;
volatile uint8 g_debugBodyCanLastTxCrc = 0U;
volatile uint32 g_debugBodyCanLastTxStatus = 0U;

IFX_INTERRUPT(BodyCan_RxIsrHandler, 0, BODY_CAN_ISR_PRIORITY_RX);

static void BodyCan_EnableTransceiver(void)
{
    IfxPort_setPinModeOutput(BODY_CAN_STB_PORT,
                             BODY_CAN_STB_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinLow(BODY_CAN_STB_PORT, BODY_CAN_STB_PIN);
}

static boolean BodyCan_IsValidBool(uint8 value)
{
    return ((value == BODY_CAN_OFF) || (value == BODY_CAN_ON));
}

static boolean BodyCan_IsValidTurnSignal(uint8 value)
{
    return ((value == BODY_CAN_TURN_OFF) ||
            (value == BODY_CAN_TURN_LEFT) ||
            (value == BODY_CAN_TURN_RIGHT) ||
            (value == BODY_CAN_TURN_HAZARD));
}

static boolean BodyCan_IsValidControlMode(uint8 value)
{
    return ((value == BODY_CAN_CONTROL_STANDBY) ||
            (value == BODY_CAN_CONTROL_REMOTE_DRIVE) ||
            (value == BODY_CAN_CONTROL_DIAGNOSTIC));
}

static boolean BodyCan_IsCommandDlc(IfxCan_DataLengthCode dataLengthCode)
{
    return (dataLengthCode == IfxCan_DataLengthCode_6);
}

static void BodyCan_EnterSafeState(void)
{
    g_currentHeadlampRaw = BODY_CAN_OFF;
    g_currentTurnSignalRaw = BODY_CAN_TURN_OFF;
    g_currentBrakeLampRaw = BODY_CAN_OFF;
    g_currentHornRaw = BODY_CAN_OFF;
    g_currentCollisionWarningRaw = BODY_CAN_OFF;
    g_currentControlModeRaw = BODY_CAN_CONTROL_STANDBY;

    BodyControl_AllOff();
}

static void BodyCan_ApplyBodyCommand(uint8 headlampRaw,
                                     uint8 turnSignalRaw,
                                     uint8 brakeLampRaw,
                                     uint8 hornRaw,
                                     uint8 collisionWarningRaw,
                                     uint8 controlModeRaw)
{
    g_currentControlModeRaw = controlModeRaw;

    if (controlModeRaw != BODY_CAN_CONTROL_REMOTE_DRIVE)
    {
        BodyCan_EnterSafeState();
        g_currentControlModeRaw = controlModeRaw;
        return;
    }

    g_currentHeadlampRaw = headlampRaw;
    g_currentTurnSignalRaw = turnSignalRaw;
    g_currentBrakeLampRaw = brakeLampRaw;
    g_currentHornRaw = hornRaw;
    g_currentCollisionWarningRaw = collisionWarningRaw;

    BodyControl_SetHeadlamp((headlampRaw == BODY_CAN_ON) ? TRUE : FALSE);
    BodyControl_SetTurnSignal(turnSignalRaw);
    BodyControl_SetBrakeLamp((brakeLampRaw == BODY_CAN_ON) ? TRUE : FALSE);
    BodyControl_SetHorn((hornRaw == BODY_CAN_ON) ? TRUE : FALSE);
    BodyControl_SetCollisionWarningLamp((collisionWarningRaw == BODY_CAN_ON) ? TRUE : FALSE);
}

static void BodyCan_PackTxBytes(const uint8 txByte[8], uint32 txData[2])
{
    txData[0] = 0U;
    txData[1] = 0U;

    txData[0] |= ((uint32)txByte[0]) << 0U;
    txData[0] |= ((uint32)txByte[1]) << 8U;
    txData[0] |= ((uint32)txByte[2]) << 16U;
    txData[0] |= ((uint32)txByte[3]) << 24U;

    txData[1] |= ((uint32)txByte[4]) << 0U;
    txData[1] |= ((uint32)txByte[5]) << 8U;
    txData[1] |= ((uint32)txByte[6]) << 16U;
    txData[1] |= ((uint32)txByte[7]) << 24U;
}

static void BodyCan_UnpackRxBytes(uint8 rxByte[8])
{
    rxByte[0] = (uint8)((g_bodyCan.rxData[0] >> 0U)  & 0xFFU);
    rxByte[1] = (uint8)((g_bodyCan.rxData[0] >> 8U)  & 0xFFU);
    rxByte[2] = (uint8)((g_bodyCan.rxData[0] >> 16U) & 0xFFU);
    rxByte[3] = (uint8)((g_bodyCan.rxData[0] >> 24U) & 0xFFU);

    rxByte[4] = (uint8)((g_bodyCan.rxData[1] >> 0U)  & 0xFFU);
    rxByte[5] = (uint8)((g_bodyCan.rxData[1] >> 8U)  & 0xFFU);
    rxByte[6] = (uint8)((g_bodyCan.rxData[1] >> 16U) & 0xFFU);
    rxByte[7] = (uint8)((g_bodyCan.rxData[1] >> 24U) & 0xFFU);
}

static IfxCan_Status BodyCan_SendRawFrame(uint32 messageId,
                                          IfxCan_DataLengthCode dataLengthCode,
                                          const uint8 txByte[8])
{
    IfxCan_Message txMsg;
    uint32 txData[2];
    IfxCan_Status status;

    BodyCan_PackTxBytes(txByte, txData);

    IfxCan_Can_initMessage(&txMsg);

    txMsg.messageId = messageId;
    txMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    txMsg.frameMode = BODY_CAN_FRAME_MODE;
    txMsg.dataLengthCode = dataLengthCode;

    txMsg.bufferNumber = g_txBufferIndex;
    txMsg.storeInTxFifoQueue = FALSE;

    status = IfxCan_Can_sendMessage(&g_bodyCan.canNode, &txMsg, txData);

    g_debugBodyCanLastTxStatus = (uint32)status;

    if (status != IfxCan_Status_notSentBusy)
    {
        g_txBufferIndex++;

        if (g_txBufferIndex >= BODY_CAN_TX_BUFFER_COUNT)
        {
            g_txBufferIndex = 0U;
        }
    }

    return status;
}

static void BodyCan_SendUdsPayload(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                   uint8 payloadLength)
{
    uint8 txByte[8];
    uint8 index;
    IfxCan_Status status;

    if ((payloadLength == 0U) ||
        (payloadLength > BODY_UDS_SF_MAX_PAYLOAD_BYTES))
    {
        g_invalidCount++;
        return;
    }

    for (index = 0U; index < 8U; index++)
    {
        txByte[index] = 0U;
    }

    txByte[0] = (uint8)(payloadLength & 0x0FU);

    for (index = 0U; index < payloadLength; index++)
    {
        txByte[index + 1U] = payload[index];
    }

    status = BodyCan_SendRawFrame(BODY_CAN_UDS_RESP_ID,
                                  IfxCan_DataLengthCode_8,
                                  txByte);

    if (status == IfxCan_Status_notSentBusy)
    {
        g_udsTxBusyCount++;
        g_debugBodyCanUdsTxBusyCount = g_udsTxBusyCount;
        return;
    }

    g_udsTxCount++;
    g_debugBodyCanUdsTxCount = g_udsTxCount;
}

static void BodyCan_SendUdsNegativeResponse(uint8 requestSid, uint8 nrc)
{
    uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 index;

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        payload[index] = 0U;
    }

    payload[0] = 0x7FU;
    payload[1] = requestSid;
    payload[2] = nrc;

    g_udsLastNrc = nrc;
    g_debugBodyCanUdsLastNrc = nrc;

    BodyCan_SendUdsPayload(payload, 3U);
}

static void BodyCan_SendUdsPositiveResponse(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                            uint8 payloadLength)
{
    g_udsLastNrc = 0U;
    g_debugBodyCanUdsLastNrc = 0U;

    BodyCan_SendUdsPayload(payload, payloadLength);
}

static void BodyCan_HandleUdsSessionControl(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                            uint8 payloadLength)
{
    uint8 response[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 index;
    uint8 requestedSession;

    if (payloadLength != 2U)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_SESSION_CONTROL,
                                        BODY_UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    requestedSession = payload[1] & 0x7FU;

    if ((requestedSession != BODY_UDS_SESSION_DEFAULT) &&
        (requestedSession != BODY_UDS_SESSION_EXTENDED))
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_SESSION_CONTROL,
                                        BODY_UDS_NRC_SUBFUNCTION_UNSUPPORTED);
        return;
    }

    g_udsSession = requestedSession;
    g_debugBodyCanUdsSession = g_udsSession;

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        response[index] = 0U;
    }

    response[0] = BODY_UDS_SID_SESSION_CONTROL + BODY_UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = requestedSession;

    BodyCan_SendUdsPositiveResponse(response, 2U);
}

static void BodyCan_HandleUdsReadDid(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                     uint8 payloadLength)
{
    uint8 response[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 index;
    uint16 did;
    uint16 distanceMm;
    uint32 distance;

    if (payloadLength != 3U)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_READ_DID,
                                        BODY_UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    did = (uint16)((((uint16)payload[1]) << 8U) | ((uint16)payload[2]));

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        response[index] = 0U;
    }

    response[0] = BODY_UDS_SID_READ_DID + BODY_UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = payload[1];
    response[2] = payload[2];

    if (did == BODY_UDS_DID_ECU_ID)
    {
        response[3] = 'B';
        response[4] = 'D';
        response[5] = 'Y';
        response[6] = '1';

        BodyCan_SendUdsPositiveResponse(response, 7U);
        return;
    }

    if (did == BODY_UDS_DID_ULTRASONIC_DISTANCE)
    {
        if (BodyControl_IsUltrasonicDistanceValid() != FALSE)
        {
            distance = ((uint32)BodyControl_GetUltrasonicDistanceCm()) * 10U;

            if (distance > 0xFFFFU)
            {
                distance = 0xFFFFU;
            }

            distanceMm = (uint16)distance;
        }
        else
        {
            distanceMm = 0xFFFFU;
        }

        response[3] = (uint8)((distanceMm >> 8U) & 0xFFU);
        response[4] = (uint8)(distanceMm & 0xFFU);

        BodyCan_SendUdsPositiveResponse(response, 5U);
        return;
    }

    if (did == BODY_UDS_DID_TURN_SIGNAL_STATUS)
    {
        response[3] = g_currentTurnSignalRaw;

        BodyCan_SendUdsPositiveResponse(response, 4U);
        return;
    }

    if (did == BODY_UDS_DID_COLLISION_SWITCH)
    {
        response[3] = (BodyControl_IsCollisionButtonPressed() != FALSE) ? 1U : 0U;

        BodyCan_SendUdsPositiveResponse(response, 4U);
        return;
    }

    if (did == BODY_UDS_DID_COLLISION_WARNING)
    {
        response[3] = (g_currentCollisionWarningRaw == BODY_CAN_ON) ? 1U : 0U;

        BodyCan_SendUdsPositiveResponse(response, 4U);
        return;
    }

    BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_READ_DID,
                                    BODY_UDS_NRC_REQUEST_OUT_OF_RANGE);
}

static void BodyCan_HandleUdsReadDtc(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                     uint8 payloadLength)
{
    uint8 response[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 index;
    uint8 statusMask;
    uint8 faultMask;

    if (payloadLength != 3U)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_READ_DTC,
                                        BODY_UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if (payload[1] != 0x02U)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_READ_DTC,
                                        BODY_UDS_NRC_SUBFUNCTION_UNSUPPORTED);
        return;
    }

    statusMask = payload[2];
    faultMask = BodyControl_GetDiagFaultMask();

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        response[index] = 0U;
    }

    response[0] = BODY_UDS_SID_READ_DTC + BODY_UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = 0x02U;
    response[2] = BODY_UDS_DTC_STATUS_AVAILABILITY_MASK;

    if (((faultMask & BODY_DIAG_ULTRASONIC_BIT) != 0U) &&
        ((statusMask & BODY_UDS_DTC_STATUS_TEST_FAILED) != 0U))
    {
        response[3] = BODY_UDS_DTC_ULTRASONIC_BYTE0;
        response[4] = BODY_UDS_DTC_ULTRASONIC_BYTE1;
        response[5] = BODY_UDS_DTC_ULTRASONIC_BYTE2;
        response[6] = BODY_UDS_DTC_STATUS_TEST_FAILED;

        BodyCan_SendUdsPositiveResponse(response, 7U);
        return;
    }

    BodyCan_SendUdsPositiveResponse(response, 3U);
}

static void BodyCan_HandleUdsClearDtc(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                      uint8 payloadLength)
{
    uint8 response[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 index;

    if (payloadLength != 4U)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_CLEAR_DTC,
                                        BODY_UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    if ((payload[1] != 0xFFU) ||
        (payload[2] != 0xFFU) ||
        (payload[3] != 0xFFU))
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_CLEAR_DTC,
                                        BODY_UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    BodyControl_ClearDiagFaults();

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        response[index] = 0U;
    }

    response[0] = BODY_UDS_SID_CLEAR_DTC + BODY_UDS_POSITIVE_RESPONSE_OFFSET;

    BodyCan_SendUdsPositiveResponse(response, 1U);
}

static void BodyCan_HandleUdsRoutineControl(const uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES],
                                            uint8 payloadLength)
{
    uint8 response[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 index;
    uint8 subFunction;
    uint16 routineId;
    uint16 distanceMm;
    boolean ultrasonicOk;

    if (payloadLength != 4U)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_ROUTINE_CONTROL,
                                        BODY_UDS_NRC_INCORRECT_LENGTH);
        return;
    }

    subFunction = payload[1] & 0x7FU;

    if (subFunction != BODY_UDS_ROUTINE_START)
    {
        BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_ROUTINE_CONTROL,
                                        BODY_UDS_NRC_SUBFUNCTION_UNSUPPORTED);
        return;
    }

    routineId = (uint16)((((uint16)payload[2]) << 8U) | ((uint16)payload[3]));

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        response[index] = 0U;
    }

    response[0] = BODY_UDS_SID_ROUTINE_CONTROL + BODY_UDS_POSITIVE_RESPONSE_OFFSET;
    response[1] = BODY_UDS_ROUTINE_START;
    response[2] = payload[2];
    response[3] = payload[3];

    if (routineId == BODY_UDS_RID_BUZZER_TEST)
    {
        BodyControl_RunBuzzerDiagnosticRoutine();
        response[4] = BODY_UDS_ROUTINE_RESULT_OK;

        BodyCan_SendUdsPositiveResponse(response, 5U);
        return;
    }

    if (routineId == BODY_UDS_RID_LED_ALL_TEST)
    {
        BodyControl_RunLedDiagnosticRoutine();
        response[4] = BODY_UDS_ROUTINE_RESULT_OK;

        BodyCan_SendUdsPositiveResponse(response, 5U);
        return;
    }

    if (routineId == BODY_UDS_RID_ULTRASONIC_TEST)
    {
        ultrasonicOk = BodyControl_RunUltrasonicDiagnosticRoutine(&distanceMm);
        response[4] = (ultrasonicOk != FALSE) ?
                      BODY_UDS_ROUTINE_RESULT_OK :
                      BODY_UDS_ROUTINE_RESULT_TIMEOUT;
        response[5] = (uint8)((distanceMm >> 8U) & 0xFFU);
        response[6] = (uint8)(distanceMm & 0xFFU);

        BodyCan_SendUdsPositiveResponse(response, 7U);
        return;
    }

    BodyCan_SendUdsNegativeResponse(BODY_UDS_SID_ROUTINE_CONTROL,
                                    BODY_UDS_NRC_REQUEST_OUT_OF_RANGE);
}

static void BodyCan_HandleUdsRequest(const uint8 rxByte[8])
{
    uint8 payload[BODY_UDS_SF_MAX_PAYLOAD_BYTES];
    uint8 payloadLength;
    uint8 index;
    uint8 sid;

    if ((rxByte[0] & 0xF0U) != 0x00U)
    {
        g_invalidCount++;
        return;
    }

    payloadLength = rxByte[0] & 0x0FU;

    if ((payloadLength == 0U) ||
        (payloadLength > BODY_UDS_SF_MAX_PAYLOAD_BYTES))
    {
        g_invalidCount++;
        return;
    }

    for (index = 0U; index < BODY_UDS_SF_MAX_PAYLOAD_BYTES; index++)
    {
        payload[index] = rxByte[index + 1U];
    }

    sid = payload[0];
    g_udsLastSid = sid;
    g_debugBodyCanUdsLastSid = sid;

    switch (sid)
    {
        case BODY_UDS_SID_SESSION_CONTROL:
            BodyCan_HandleUdsSessionControl(payload, payloadLength);
            break;

        case BODY_UDS_SID_READ_DID:
            BodyCan_HandleUdsReadDid(payload, payloadLength);
            break;

        case BODY_UDS_SID_READ_DTC:
            BodyCan_HandleUdsReadDtc(payload, payloadLength);
            break;

        case BODY_UDS_SID_CLEAR_DTC:
            BodyCan_HandleUdsClearDtc(payload, payloadLength);
            break;

        case BODY_UDS_SID_ROUTINE_CONTROL:
            BodyCan_HandleUdsRoutineControl(payload, payloadLength);
            break;

        default:
            BodyCan_SendUdsNegativeResponse(sid,
                                            BODY_UDS_NRC_SERVICE_NOT_SUPPORTED);
            break;
    }
}

static void BodyCan_ProcessUdsRequestIfNeeded(void)
{
    uint8 rxByte[8];
    uint8 index;

    if (g_udsRequestUpdated == FALSE)
    {
        return;
    }

    for (index = 0U; index < 8U; index++)
    {
        rxByte[index] = g_udsRequestByte[index];
    }

    g_udsRequestUpdated = FALSE;
    BodyCan_HandleUdsRequest(rxByte);
}

static void BodyCan_SendHeartbeat(void)
{
    uint8 txByte[8];
    IfxCan_Status status;

    g_debugBodyCanStatusTxCallCount++;

    txByte[0] = g_txHeartbeatAliveCounter;
    txByte[1] = 0U;
    txByte[2] = 0U;
    txByte[3] = 0U;
    txByte[4] = 0U;
    txByte[5] = 0U;
    txByte[6] = 0U;
    txByte[7] = 0U;

    status = BodyCan_SendRawFrame(BODY_CAN_HEARTBEAT_ID,
                                  IfxCan_DataLengthCode_1,
                                  txByte);

    if (status == IfxCan_Status_notSentBusy)
    {
        g_debugBodyCanStatusTxBusyCount++;
        return;
    }

    g_debugBodyCanTxAliveCounter = g_txHeartbeatAliveCounter;
    g_debugBodyCanLastTxCrc = 0U;
    g_txHeartbeatAliveCounter++;
    g_debugBodyCanStatusTxOkCount++;
}

static void BodyCan_SendHeartbeatIfNeeded(void)
{
    g_heartbeatTxTimerMs++;

    if (g_heartbeatTxTimerMs < BODY_CAN_HEARTBEAT_TX_PERIOD_MS)
    {
        return;
    }

    g_heartbeatTxTimerMs = 0U;
    BodyCan_SendHeartbeat();
}

static void BodyCan_SendPendingCollisionEventIfNeeded(void)
{
    uint32 okCountBefore;

    if (g_collisionEventPending == FALSE)
    {
        return;
    }

    okCountBefore = g_debugBodyCanEventTxOkCount;

    BodyCan_SendCollisionEvent(g_pendingCollisionOccurrenceTimeMs);

    if (g_debugBodyCanEventTxOkCount != okCountBefore)
    {
        g_collisionEventPending = FALSE;
        g_debugBodyCanEventPending = 0U;
    }
}

void BodyCan_RxIsrHandler(void)
{
    uint8 rxByte[8];
    uint8 index;
    uint8 headlampRaw;
    uint8 turnSignalRaw;
    uint8 brakeLampRaw;
    uint8 hornRaw;
    uint8 collisionWarningRaw;
    uint8 controlModeRaw;

    IfxCan_Node_clearInterruptFlag(g_bodyCan.canNode.node,
                                   IfxCan_Interrupt_messageStoredToDedicatedRxBuffer);

    IfxCan_Can_initMessage(&g_bodyCan.rxMsg);
    g_bodyCan.rxMsg.bufferNumber = BODY_CAN_RX_BUFFER_ID;

    IfxCan_Can_readMessage(&g_bodyCan.canNode,
                           &g_bodyCan.rxMsg,
                           &g_bodyCan.rxData[0]);

    IfxCan_Node_clearRxBufferNewDataFlag(g_bodyCan.canNode.node,
                                         BODY_CAN_RX_BUFFER_ID);

    BodyCan_UnpackRxBytes(rxByte);

    if (g_bodyCan.rxMsg.messageId == BODY_CAN_UDS_REQ_ID)
    {
        if (g_bodyCan.rxMsg.dataLengthCode != IfxCan_DataLengthCode_8)
        {
            g_invalidCount++;
            return;
        }

        for (index = 0U; index < 8U; index++)
        {
            g_udsRequestByte[index] = rxByte[index];
        }

        g_udsRequestUpdated = TRUE;
        g_udsRxCount++;
        g_debugBodyCanUdsRxCount = g_udsRxCount;
        return;
    }

    if (g_bodyCan.rxMsg.messageId != BODY_CAN_CMD_ID)
    {
        g_invalidCount++;
        return;
    }

    if (BodyCan_IsCommandDlc(g_bodyCan.rxMsg.dataLengthCode) == FALSE)
    {
        g_invalidCount++;
        return;
    }

    headlampRaw = rxByte[0];
    turnSignalRaw = rxByte[1];
    brakeLampRaw = rxByte[2];
    hornRaw = rxByte[3];
    collisionWarningRaw = rxByte[4];
    controlModeRaw = rxByte[5];

    if ((BodyCan_IsValidBool(headlampRaw) == FALSE) ||
        (BodyCan_IsValidTurnSignal(turnSignalRaw) == FALSE) ||
        (BodyCan_IsValidBool(brakeLampRaw) == FALSE) ||
        (BodyCan_IsValidBool(hornRaw) == FALSE) ||
        (BodyCan_IsValidBool(collisionWarningRaw) == FALSE) ||
        (BodyCan_IsValidControlMode(controlModeRaw) == FALSE))
    {
        g_invalidCount++;
        return;
    }

    g_cmdTimeoutMs = 0U;

    g_rxHeadlampRaw = headlampRaw;
    g_rxTurnSignalRaw = turnSignalRaw;
    g_rxBrakeLampRaw = brakeLampRaw;
    g_rxHornRaw = hornRaw;
    g_rxCollisionWarningRaw = collisionWarningRaw;
    g_rxControlModeRaw = controlModeRaw;

    g_debugBodyCanLastRxCrc = 0U;
    g_debugBodyCanRxAliveCounter = 0U;

    g_rxUpdated = TRUE;
    g_rxCount++;
}

void BodyCan_Init(void)
{
    BodyCan_EnableTransceiver();

    IfxCan_Can_initModuleConfig(&g_bodyCan.canConfig, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_bodyCan.canModule, &g_bodyCan.canConfig);

    IfxCan_Can_initNodeConfig(&g_bodyCan.nodeConfig, &g_bodyCan.canModule);

    g_bodyCan.nodeConfig.busLoopbackEnabled = FALSE;
    g_bodyCan.nodeConfig.nodeId = IfxCan_NodeId_0;
    g_bodyCan.nodeConfig.clockSource = IfxCan_ClockSource_both;
    g_bodyCan.nodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    g_bodyCan.nodeConfig.frame.mode = BODY_CAN_FRAME_MODE;
    g_bodyCan.nodeConfig.baudRate.baudrate = BODY_CAN_BAUDRATE;
    g_bodyCan.nodeConfig.fastBaudRate.baudrate = BODY_CAN_FD_DATA_BAUDRATE;

    g_bodyCan.nodeConfig.txConfig.txMode = IfxCan_TxMode_dedicatedBuffers;
    g_bodyCan.nodeConfig.txConfig.dedicatedTxBuffersNumber = BODY_CAN_TX_BUFFER_COUNT;
    g_bodyCan.nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_64;

    g_bodyCan.nodeConfig.rxConfig.rxMode = IfxCan_RxMode_dedicatedBuffers;
    g_bodyCan.nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_64;

    g_bodyCan.nodeConfig.filterConfig.messageIdLength = IfxCan_MessageIdLength_standard;
    g_bodyCan.nodeConfig.filterConfig.standardListSize = 2U;
    g_bodyCan.nodeConfig.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;
    g_bodyCan.nodeConfig.filterConfig.rejectRemoteFramesWithStandardId = TRUE;
    g_bodyCan.nodeConfig.filterConfig.rejectRemoteFramesWithExtendedId = TRUE;

    g_bodyCan.nodeConfig.messageRAM.baseAddress = BODY_CAN_MODULE_RAM_BASE;
    g_bodyCan.nodeConfig.messageRAM.standardFilterListStartAddress = 0x000U;
    g_bodyCan.nodeConfig.messageRAM.rxBuffersStartAddress = 0x100U;
    g_bodyCan.nodeConfig.messageRAM.txBuffersStartAddress = 0x200U;

    g_bodyCan.nodeConfig.interruptConfig.messageStoredToDedicatedRxBufferEnabled = TRUE;
    g_bodyCan.nodeConfig.interruptConfig.reint.priority = BODY_CAN_ISR_PRIORITY_RX;
    g_bodyCan.nodeConfig.interruptConfig.reint.interruptLine = IfxCan_InterruptLine_1;
    g_bodyCan.nodeConfig.interruptConfig.reint.typeOfService = IfxSrc_Tos_cpu0;

    g_bodyCan.nodeConfig.pins = &g_bodyCanPins;

    IfxCan_Can_initNode(&g_bodyCan.canNode, &g_bodyCan.nodeConfig);

    g_bodyCan.canFilter[0].number = 0U;
    g_bodyCan.canFilter[0].elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxBuffer;
    g_bodyCan.canFilter[0].type = IfxCan_FilterType_none;
    g_bodyCan.canFilter[0].id1 = BODY_CAN_CMD_ID;
    g_bodyCan.canFilter[0].id2 = BODY_CAN_CMD_ID;
    g_bodyCan.canFilter[0].rxBufferOffset = BODY_CAN_RX_BUFFER_ID;

    IfxCan_Can_setStandardFilter(&g_bodyCan.canNode, &g_bodyCan.canFilter[0]);

    g_bodyCan.canFilter[1].number = 1U;
    g_bodyCan.canFilter[1].elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxBuffer;
    g_bodyCan.canFilter[1].type = IfxCan_FilterType_none;
    g_bodyCan.canFilter[1].id1 = BODY_CAN_UDS_REQ_ID;
    g_bodyCan.canFilter[1].id2 = BODY_CAN_UDS_REQ_ID;
    g_bodyCan.canFilter[1].rxBufferOffset = BODY_CAN_RX_BUFFER_ID;

    IfxCan_Can_setStandardFilter(&g_bodyCan.canNode, &g_bodyCan.canFilter[1]);

    g_rxHeadlampRaw = BODY_CAN_OFF;
    g_rxTurnSignalRaw = BODY_CAN_TURN_OFF;
    g_rxBrakeLampRaw = BODY_CAN_OFF;
    g_rxHornRaw = BODY_CAN_OFF;
    g_rxCollisionWarningRaw = BODY_CAN_OFF;
    g_rxControlModeRaw = BODY_CAN_CONTROL_STANDBY;

    g_rxUpdated = FALSE;

    g_rxCount = 0U;
    g_invalidCount = 0U;
    g_crcErrorCount = 0U;
    g_aliveErrorCount = 0U;

    g_currentHeadlampRaw = BODY_CAN_OFF;
    g_currentTurnSignalRaw = BODY_CAN_TURN_OFF;
    g_currentBrakeLampRaw = BODY_CAN_OFF;
    g_currentHornRaw = BODY_CAN_OFF;
    g_currentCollisionWarningRaw = BODY_CAN_OFF;
    g_currentControlModeRaw = BODY_CAN_CONTROL_STANDBY;
    g_currentRxAliveCounter = 0U;

    g_cmdTimeoutMs = BODY_CAN_CMD_TIMEOUT_MS;

    g_txBufferIndex = 0U;
    g_txHeartbeatAliveCounter = 0U;
    g_heartbeatTxTimerMs = 0U;
    g_bodyCanTimeMs = 0U;

    g_collisionEventPending = FALSE;
    g_pendingCollisionOccurrenceTimeMs = 0U;

    g_udsRequestUpdated = FALSE;
    g_udsRequestByte[0] = 0U;
    g_udsRequestByte[1] = 0U;
    g_udsRequestByte[2] = 0U;
    g_udsRequestByte[3] = 0U;
    g_udsRequestByte[4] = 0U;
    g_udsRequestByte[5] = 0U;
    g_udsRequestByte[6] = 0U;
    g_udsRequestByte[7] = 0U;
    g_udsSession = BODY_UDS_SESSION_DEFAULT;
    g_udsRxCount = 0U;
    g_udsTxCount = 0U;
    g_udsTxBusyCount = 0U;
    g_udsLastSid = 0U;
    g_udsLastNrc = 0U;

    g_debugBodyCanRxCount = 0U;
    g_debugBodyCanInvalidCount = 0U;
    g_debugBodyCanCrcErrorCount = 0U;
    g_debugBodyCanAliveErrorCount = 0U;

    g_debugBodyCanStatusTxCallCount = 0U;
    g_debugBodyCanStatusTxOkCount = 0U;
    g_debugBodyCanStatusTxBusyCount = 0U;
    g_debugBodyCanEventTxCallCount = 0U;
    g_debugBodyCanEventTxOkCount = 0U;
    g_debugBodyCanEventTxBusyCount = 0U;
    g_debugBodyCanEventPending = 0U;
    g_debugBodyCanLastCollisionTimeMs = 0U;

    g_debugBodyCanUdsRxCount = 0U;
    g_debugBodyCanUdsTxCount = 0U;
    g_debugBodyCanUdsTxBusyCount = 0U;
    g_debugBodyCanUdsSession = BODY_UDS_SESSION_DEFAULT;
    g_debugBodyCanUdsLastSid = 0U;
    g_debugBodyCanUdsLastNrc = 0U;

    g_debugBodyCanRxAliveCounter = 0U;
    g_debugBodyCanTxAliveCounter = 0U;
    g_debugBodyCanLastRxCrc = 0U;
    g_debugBodyCanLastTxCrc = 0U;
    g_debugBodyCanLastTxStatus = 0U;

    BodyCan_EnterSafeState();
}

void BodyCan_Update1ms(void)
{
    uint8 headlampRaw;
    uint8 turnSignalRaw;
    uint8 brakeLampRaw;
    uint8 hornRaw;
    uint8 collisionWarningRaw;
    uint8 controlModeRaw;

    g_bodyCanTimeMs++;

    if (g_cmdTimeoutMs < BODY_CAN_CMD_TIMEOUT_MS)
    {
        g_cmdTimeoutMs++;
    }

    if (g_cmdTimeoutMs >= BODY_CAN_CMD_TIMEOUT_MS)
    {
        BodyCan_EnterSafeState();
    }

    if (g_rxUpdated == TRUE)
    {
        headlampRaw = g_rxHeadlampRaw;
        turnSignalRaw = g_rxTurnSignalRaw;
        brakeLampRaw = g_rxBrakeLampRaw;
        hornRaw = g_rxHornRaw;
        collisionWarningRaw = g_rxCollisionWarningRaw;
        controlModeRaw = g_rxControlModeRaw;

        g_rxUpdated = FALSE;
        g_currentRxAliveCounter = 0U;

        BodyCan_ApplyBodyCommand(headlampRaw,
                                 turnSignalRaw,
                                 brakeLampRaw,
                                 hornRaw,
                                 collisionWarningRaw,
                                 controlModeRaw);
    }

    BodyCan_ProcessUdsRequestIfNeeded();
    BodyCan_SendPendingCollisionEventIfNeeded();
    BodyCan_SendHeartbeatIfNeeded();

    g_debugBodyCanRxCount = g_rxCount;
    g_debugBodyCanInvalidCount = g_invalidCount;
    g_debugBodyCanCrcErrorCount = g_crcErrorCount;
    g_debugBodyCanAliveErrorCount = g_aliveErrorCount;
    g_debugBodyCanUdsRxCount = g_udsRxCount;
    g_debugBodyCanUdsTxCount = g_udsTxCount;
    g_debugBodyCanUdsTxBusyCount = g_udsTxBusyCount;
    g_debugBodyCanUdsSession = g_udsSession;
    g_debugBodyCanUdsLastSid = g_udsLastSid;
    g_debugBodyCanUdsLastNrc = g_udsLastNrc;
}

void BodyCan_SendCollisionEvent(uint32 occurrenceTimeMs)
{
    uint8 txByte[8];
    IfxCan_Status status;

    g_debugBodyCanEventTxCallCount++;

    txByte[0] = 1U;
    txByte[1] = (uint8)((occurrenceTimeMs >> 24U) & 0xFFU);
    txByte[2] = (uint8)((occurrenceTimeMs >> 16U) & 0xFFU);
    txByte[3] = (uint8)((occurrenceTimeMs >> 8U)  & 0xFFU);
    txByte[4] = (uint8)((occurrenceTimeMs >> 0U)  & 0xFFU);
    txByte[5] = 0U;
    txByte[6] = 0U;
    txByte[7] = 0U;

    status = BodyCan_SendRawFrame(BODY_CAN_COLLISION_EVENT_ID,
                                  IfxCan_DataLengthCode_5,
                                  txByte);

    if (status == IfxCan_Status_notSentBusy)
    {
        g_debugBodyCanEventTxBusyCount++;
        return;
    }

    g_debugBodyCanLastCollisionTimeMs = occurrenceTimeMs;
    g_debugBodyCanEventTxOkCount++;
}

void BodyCan_ReportCollisionOccurred(void)
{
    if (g_collisionEventPending == FALSE)
    {
        g_pendingCollisionOccurrenceTimeMs = g_bodyCanTimeMs;
    }

    g_collisionEventPending = TRUE;
    g_debugBodyCanEventPending = 1U;
}

uint32 BodyCan_GetRxCount(void)
{
    return g_rxCount;
}

uint32 BodyCan_GetInvalidCount(void)
{
    return g_invalidCount;
}

uint32 BodyCan_GetCrcErrorCount(void)
{
    return g_crcErrorCount;
}

uint32 BodyCan_GetAliveErrorCount(void)
{
    return g_aliveErrorCount;
}

uint8 BodyCan_GetHeadlamp(void)
{
    return g_currentHeadlampRaw;
}

uint8 BodyCan_GetTurnSignal(void)
{
    return g_currentTurnSignalRaw;
}

uint8 BodyCan_GetBrakeLamp(void)
{
    return g_currentBrakeLampRaw;
}

uint8 BodyCan_GetHorn(void)
{
    return g_currentHornRaw;
}

uint8 BodyCan_GetCollisionWarningLamp(void)
{
    return g_currentCollisionWarningRaw;
}

uint8 BodyCan_GetControlMode(void)
{
    return g_currentControlModeRaw;
}

uint8 BodyCan_GetRxAliveCounter(void)
{
    return g_currentRxAliveCounter;
}

uint8 BodyCan_GetTxAliveCounter(void)
{
    return g_txHeartbeatAliveCounter;
}

uint32 BodyCan_GetTimeMs(void)
{
    return g_bodyCanTimeMs;
}

uint32 BodyCan_GetUdsRxCount(void)
{
    return g_udsRxCount;
}

uint32 BodyCan_GetUdsTxCount(void)
{
    return g_udsTxCount;
}

uint8 BodyCan_GetUdsSession(void)
{
    return g_udsSession;
}
