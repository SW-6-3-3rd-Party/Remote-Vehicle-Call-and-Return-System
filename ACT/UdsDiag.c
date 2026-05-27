#include "UdsDiag.h"

#include "Ifx_Types.h"
#include "IfxCan_Can.h"

#include "ActCan.h"
#include "Motor.h"
#include "Servo.h"
#include "Encoder.h"
#include "PotAdc.h"

/*
 * ======================================================
 * UDS Service Support
 *
 * 0x10 DiagnosticSessionControl
 *   0x01 DefaultSession
 *   0x03 ExtendedSession
 *
 * 0x22 ReadDataByIdentifier
 *   0xF190 = ECU ID "ACT1"
 *   0x0100 = Vehicle speed km/h x100, uint16
 *   0x0101 = Steering angle deg, int16
 *
 * 0x19 ReadDTCInformation
 *   0x0A ReportDTC
 *   Response includes every supported DTC and its full status byte.
 *
 * 0x14 ClearDiagnosticInformation
 *   0xFFFFFF = clear stored/history status bit
 *
 * 0x31 RoutineControl
 *   0x0100 = Motor test
 *   0x0101 = Servo test
 * ======================================================
 */

/* =========================
 * UDS SID
 * ========================= */
#define SID_DIAGNOSTIC_SESSION_CONTROL       (0x10U)
#define SID_READ_DATA_BY_IDENTIFIER          (0x22U)
#define SID_READ_DTC_INFORMATION             (0x19U)
#define SID_CLEAR_DIAGNOSTIC_INFORMATION     (0x14U)
#define SID_ROUTINE_CONTROL                  (0x31U)

#define POS_RESP_OFFSET                      (0x40U)
#define SID_NEGATIVE_RESPONSE                (0x7FU)

/* =========================
 * UDS NRC
 * ========================= */
#define NRC_GENERAL_REJECT                   (0x10U)
#define NRC_SERVICE_NOT_SUPPORTED            (0x11U)
#define NRC_SUBFUNCTION_NOT_SUPPORTED        (0x12U)
#define NRC_INCORRECT_MESSAGE_LENGTH         (0x13U)
#define NRC_BUSY_REPEAT_REQUEST              (0x21U)
#define NRC_REQUEST_OUT_OF_RANGE             (0x31U)
#define NRC_CONDITIONS_NOT_CORRECT           (0x22U)

/* =========================
 * Session
 * ========================= */
#define SESSION_DEFAULT                      (0x01U)
#define SESSION_EXTENDED                     (0x03U)
#define SESSION_P2_SERVER_MAX_H              (0x27U)
#define SESSION_P2_SERVER_MAX_L              (0x10U)
#define SESSION_P2_EXT_SERVER_MAX_H          (0x03U)
#define SESSION_P2_EXT_SERVER_MAX_L          (0xE8U)

static uint8 g_diagSession = SESSION_DEFAULT;

/* =========================
 * DID
 * ========================= */
#define DID_ECU_ID                           (0xF190U)
#define DID_VEHICLE_SPEED                    (0x0100U)
#define DID_STEERING_ANGLE                   (0x0101U)

/* =========================
 * DTC
 * ========================= */
#define DTC_ENCODER_NO_SIGNAL                (0xC10000UL)
#define DTC_STEERING_MISMATCH                (0xC10100UL)

#define DTC_REPORT_SUBFUNCTION               (0x0AU)

/*
 * Custom DTC status:
 * bit0 = current fault
 * bit3 = stored/history fault
 */
#define DTC_STATUS_INIT                      (0x00U)
#define DTC_STATUS_CURRENT_BIT               (0x01U)
#define DTC_STATUS_HISTORY_BIT               (0x08U)
#define DTC_STATUS_AVAILABILITY_MASK         (DTC_STATUS_CURRENT_BIT | DTC_STATUS_HISTORY_BIT)

#define DTC_MONITOR_PERIOD_MS                (3000U)
#define DTC_CONFIRM_FAIL_COUNT               (3U)
#define DTC_ENCODER_NO_SIGNAL_MS             (2000U)
#define DTC_STEERING_MISMATCH_MS             (1000U)

static uint8 g_dtcC100Status = DTC_STATUS_INIT;
static uint8 g_dtcC101Status = DTC_STATUS_INIT;

static uint32 g_dtcEncoderNoSignalTimerMs = 0U;
static uint32 g_dtcSteeringMismatchTimerMs = 0U;
static uint32 g_dtcMonitorPeriodTimerMs = 0U;
static uint8 g_dtcC100ConsecutiveFailCount = 0U;
static uint8 g_dtcC101ConsecutiveFailCount = 0U;

/* =========================
 * Routine
 * ========================= */
#define ROUTINE_MOTOR_TEST                   (0x0100U)
#define ROUTINE_SERVO_TEST                   (0x0101U)

#define ROUTINE_RESULT_OK                    (0x00U)

typedef enum
{
    UDS_ROUTINE_NONE = 0,
    UDS_ROUTINE_MOTOR_TEST,
    UDS_ROUTINE_SERVO_TEST
} UdsRoutineState;

static UdsRoutineState g_routineState = UDS_ROUTINE_NONE;
static uint32 g_routineTimerMs = 0U;
static uint32 g_routineStep = 0U;
static uint8 g_routineResult = ROUTINE_RESULT_OK;
static uint32 g_savedMotorDuty = 90U;

/* =========================
 * Request queue
 * ========================= */
static volatile boolean g_reqPending = FALSE;
static volatile uint8 g_reqData[8];

/* =========================
 * TX buffer
 * Status uses buffer 0/1.
 * UDS uses buffer 2/3.
 * ========================= */
#define UDS_TX_BUFFER_0                      (2U)
#define UDS_TX_BUFFER_1                      (3U)

#define UDS_CAN_RESPONSE_DLC_8_BYTES         (8U)
#define UDS_CAN_RESPONSE_DLC_12_BYTES        (12U)
#define UDS_SINGLE_FRAME_MAX_PAYLOAD         (11U)

static uint8 g_udsTxBufferIndex = UDS_TX_BUFFER_0;

/* =========================
 * Debug
 * ========================= */
volatile uint32 g_debugUdsRxCount = 0U;
volatile uint32 g_debugUdsTxCount = 0U;
volatile uint32 g_debugUdsTxBusyCount = 0U;
volatile uint32 g_debugUdsLastSid = 0U;
volatile uint32 g_debugUdsLastNrc = 0U;
volatile uint32 g_debugUdsDtcC100 = 0U;
volatile uint32 g_debugUdsDtcC101 = 0U;
volatile uint32 g_debugUdsRoutineState = 0U;
volatile uint32 g_debugUdsRoutineResult = 0U;

/* ======================================================
 * Utility
 * ====================================================== */
static uint32 UdsDiag_AbsDiffSint16(sint16 a, sint16 b)
{
    sint32 diff;

    diff = (sint32)a - (sint32)b;

    if (diff < 0)
    {
        diff = -diff;
    }

    return (uint32)diff;
}

static uint16 UdsDiag_GetSpeedKmhX100(void)
{
    /*
     * Cpu0_Main.c와 같은 계산식.
     * wheel diameter = 0.065m
     * pulses per rev = 990
     */
    float wheelCircumferenceM;
    float speedMps;
    float speedKmh;
    float speedKmhX100;
    uint32 pulsePerSecond;

    pulsePerSecond = Encoder_GetPulsePerSecond();

    wheelCircumferenceM = 3.1415926f * 0.065f;
    speedMps = ((float)pulsePerSecond / 990.0f) * wheelCircumferenceM;
    speedKmh = speedMps * 3.6f;
    speedKmhX100 = speedKmh * 100.0f * 100.0f;

    if (speedKmhX100 < 0.0f)
    {
        speedKmhX100 = 0.0f;
    }

    if (speedKmhX100 > 65535.0f)
    {
        speedKmhX100 = 65535.0f;
    }

    return (uint16)(speedKmhX100 + 0.5f);
}

static sint16 UdsDiag_GetSteeringAngleDeg(void)
{
    sint16 angle;

    angle = PotAdc_GetSignedAngleDeg();

    if (angle < -45)
    {
        angle = -45;
    }

    if (angle > 45)
    {
        angle = 45;
    }

    return angle;
}

static sint16 UdsDiag_GetCommandTargetAngleDeg(void)
{
    SteeringKey key;

    key = Steering_GetKey();

    switch (key)
    {
        case STEERING_KEY_LEFT:
            return -35;

        case STEERING_KEY_RIGHT:
            return 35;

        case STEERING_KEY_NULL:
        default:
            return 0;
    }
}

/* ======================================================
 * CAN Response
 * ISO-TP Single Frame only.
 * CAN FD response supports up to 11 UDS payload bytes in DLC 12.
 * ====================================================== */
static void UdsDiag_SendSingleFrame(const uint8* udsPayload, uint8 udsLen)
{
    IfxCan_Message txMsg;
    uint32 txData[3];
    uint8 txByte[UDS_CAN_RESPONSE_DLC_12_BYTES];
    uint8 i;
    uint8 dlcBytes;
    IfxCan_Status status;

    if ((udsLen == 0U) || (udsLen > UDS_SINGLE_FRAME_MAX_PAYLOAD))
    {
        return;
    }

    if (udsLen <= 7U)
    {
        dlcBytes = UDS_CAN_RESPONSE_DLC_8_BYTES;
    }
    else
    {
        dlcBytes = UDS_CAN_RESPONSE_DLC_12_BYTES;
    }

    for (i = 0U; i < UDS_CAN_RESPONSE_DLC_12_BYTES; i++)
    {
        txByte[i] = 0U;
    }

    txByte[0] = udsLen & 0x0FU;

    for (i = 0U; i < udsLen; i++)
    {
        txByte[i + 1U] = udsPayload[i];
    }

    txData[0] = 0U;
    txData[1] = 0U;
    txData[2] = 0U;

    txData[0] |= ((uint32)txByte[0]) << 0U;
    txData[0] |= ((uint32)txByte[1]) << 8U;
    txData[0] |= ((uint32)txByte[2]) << 16U;
    txData[0] |= ((uint32)txByte[3]) << 24U;

    txData[1] |= ((uint32)txByte[4]) << 0U;
    txData[1] |= ((uint32)txByte[5]) << 8U;
    txData[1] |= ((uint32)txByte[6]) << 16U;
    txData[1] |= ((uint32)txByte[7]) << 24U;

    txData[2] |= ((uint32)txByte[8]) << 0U;
    txData[2] |= ((uint32)txByte[9]) << 8U;
    txData[2] |= ((uint32)txByte[10]) << 16U;
    txData[2] |= ((uint32)txByte[11]) << 24U;

    IfxCan_Can_initMessage(&txMsg);

    txMsg.messageId = UDS_DIAG_RES_CAN_ID;
    txMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    txMsg.frameMode = IfxCan_FrameMode_fdLongAndFast;
    txMsg.dataLengthCode = (dlcBytes == UDS_CAN_RESPONSE_DLC_8_BYTES) ?
                            IfxCan_DataLengthCode_8 :
                            IfxCan_DataLengthCode_12;

    txMsg.bufferNumber = g_udsTxBufferIndex;
    txMsg.storeInTxFifoQueue = FALSE;

    status = IfxCan_Can_sendMessage(ActCan_GetCanNode(), &txMsg, txData);

    if (status == IfxCan_Status_notSentBusy)
    {
        g_debugUdsTxBusyCount++;
        return;
    }

    g_debugUdsTxCount++;

    if (g_udsTxBufferIndex == UDS_TX_BUFFER_0)
    {
        g_udsTxBufferIndex = UDS_TX_BUFFER_1;
    }
    else
    {
        g_udsTxBufferIndex = UDS_TX_BUFFER_0;
    }
}

static void UdsDiag_SendNegativeResponse(uint8 sid, uint8 nrc)
{
    uint8 payload[3];

    payload[0] = SID_NEGATIVE_RESPONSE;
    payload[1] = sid;
    payload[2] = nrc;

    g_debugUdsLastNrc = nrc;

    UdsDiag_SendSingleFrame(payload, 3U);
}

/* ======================================================
 * Service Handlers
 * ====================================================== */
static void UdsDiag_HandleSessionControl(const uint8* uds, uint8 len)
{
    uint8 payload[6];
    uint8 sub;

    if (len != 2U)
    {
        UdsDiag_SendNegativeResponse(SID_DIAGNOSTIC_SESSION_CONTROL,
                                     NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    sub = uds[1] & 0x7FU;

    if ((sub != SESSION_DEFAULT) && (sub != SESSION_EXTENDED))
    {
        UdsDiag_SendNegativeResponse(SID_DIAGNOSTIC_SESSION_CONTROL,
                                     NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    g_diagSession = sub;

    payload[0] = SID_DIAGNOSTIC_SESSION_CONTROL + POS_RESP_OFFSET;
    payload[1] = sub;
    payload[2] = SESSION_P2_SERVER_MAX_H;
    payload[3] = SESSION_P2_SERVER_MAX_L;
    payload[4] = SESSION_P2_EXT_SERVER_MAX_H;
    payload[5] = SESSION_P2_EXT_SERVER_MAX_L;

    UdsDiag_SendSingleFrame(payload, 6U);
}

static void UdsDiag_HandleReadDID(const uint8* uds, uint8 len)
{
    uint8 payload[7];
    uint16 did;
    uint16 speed;
    sint16 angle;
    uint16 angleRaw;

    if (len != 3U)
    {
        UdsDiag_SendNegativeResponse(SID_READ_DATA_BY_IDENTIFIER,
                                     NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    did = ((uint16)uds[1] << 8U) | (uint16)uds[2];

    switch (did)
    {
        case DID_ECU_ID:
            /*
             * 6 UDS bytes:
             * 62 F1 90 41 43 54 31
             */
            payload[0] = SID_READ_DATA_BY_IDENTIFIER + POS_RESP_OFFSET;
            payload[1] = 0xF1U;
            payload[2] = 0x90U;
            payload[3] = 'A';
            payload[4] = 'C';
            payload[5] = 'T';
            payload[6] = '1';
            UdsDiag_SendSingleFrame(payload, 7U);
            break;

        case DID_VEHICLE_SPEED:
            speed = UdsDiag_GetSpeedKmhX100();

            payload[0] = SID_READ_DATA_BY_IDENTIFIER + POS_RESP_OFFSET;
            payload[1] = 0x01U;
            payload[2] = 0x00U;
            payload[3] = (uint8)((speed >> 8U) & 0xFFU);
            payload[4] = (uint8)(speed & 0xFFU);

            UdsDiag_SendSingleFrame(payload, 5U);
            break;

        case DID_STEERING_ANGLE:
            angle = UdsDiag_GetSteeringAngleDeg();
            angleRaw = (uint16)angle;

            payload[0] = SID_READ_DATA_BY_IDENTIFIER + POS_RESP_OFFSET;
            payload[1] = 0x01U;
            payload[2] = 0x01U;
            payload[3] = (uint8)((angleRaw >> 8U) & 0xFFU);
            payload[4] = (uint8)(angleRaw & 0xFFU);

            UdsDiag_SendSingleFrame(payload, 5U);
            break;

        default:
            UdsDiag_SendNegativeResponse(SID_READ_DATA_BY_IDENTIFIER,
                                         NRC_REQUEST_OUT_OF_RANGE);
            break;
    }
}

static uint8 UdsDiag_AppendDtc(uint8* payload,
                               uint8 payloadIndex,
                               uint32 dtc,
                               uint8 status)
{
    payload[payloadIndex] = (uint8)((dtc >> 16U) & 0xFFU);
    payload[payloadIndex + 1U] = (uint8)((dtc >> 8U) & 0xFFU);
    payload[payloadIndex + 2U] = (uint8)(dtc & 0xFFU);
    payload[payloadIndex + 3U] = status;

    return payloadIndex + 4U;
}

static void UdsDiag_HandleReadDTC(const uint8* uds, uint8 len)
{
    uint8 payload[UDS_SINGLE_FRAME_MAX_PAYLOAD];
    uint8 payloadLen;

    if (len != 2U)
    {
        UdsDiag_SendNegativeResponse(SID_READ_DTC_INFORMATION,
                                     NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    if (uds[1] != DTC_REPORT_SUBFUNCTION)
    {
        UdsDiag_SendNegativeResponse(SID_READ_DTC_INFORMATION,
                                     NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    payload[0] = SID_READ_DTC_INFORMATION + POS_RESP_OFFSET;
    payload[1] = DTC_REPORT_SUBFUNCTION;
    payload[2] = DTC_STATUS_AVAILABILITY_MASK;
    payloadLen = 3U;

    payloadLen = UdsDiag_AppendDtc(payload,
                                   payloadLen,
                                   DTC_ENCODER_NO_SIGNAL,
                                   g_dtcC100Status);

    payloadLen = UdsDiag_AppendDtc(payload,
                                   payloadLen,
                                   DTC_STEERING_MISMATCH,
                                   g_dtcC101Status);

    UdsDiag_SendSingleFrame(payload, payloadLen);
}

static void UdsDiag_HandleClearDTC(const uint8* uds, uint8 len)
{
    uint8 payload[1];

    if (len != 4U)
    {
        UdsDiag_SendNegativeResponse(SID_CLEAR_DIAGNOSTIC_INFORMATION,
                                     NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    if ((uds[1] != 0xFFU) || (uds[2] != 0xFFU) || (uds[3] != 0xFFU))
    {
        UdsDiag_SendNegativeResponse(SID_CLEAR_DIAGNOSTIC_INFORMATION,
                                     NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    g_dtcC100Status &= (uint8)(~DTC_STATUS_HISTORY_BIT);
    g_dtcC101Status &= (uint8)(~DTC_STATUS_HISTORY_BIT);
    g_dtcC100ConsecutiveFailCount = 0U;
    g_dtcC101ConsecutiveFailCount = 0U;

    g_debugUdsDtcC100 = g_dtcC100Status;
    g_debugUdsDtcC101 = g_dtcC101Status;

    payload[0] = SID_CLEAR_DIAGNOSTIC_INFORMATION + POS_RESP_OFFSET;

    UdsDiag_SendSingleFrame(payload, 1U);
}

static void UdsDiag_StartMotorRoutine(void)
{
    g_routineState = UDS_ROUTINE_MOTOR_TEST;
    g_routineTimerMs = 0U;
    g_routineStep = 0U;
    g_routineResult = ROUTINE_RESULT_OK;

    g_savedMotorDuty = MotorControl_GetDutyPercent();

    Encoder_ResetCount();
    MotorControl_SetDutyPercent(30U);
}

static void UdsDiag_StartServoRoutine(void)
{
    g_routineState = UDS_ROUTINE_SERVO_TEST;
    g_routineTimerMs = 0U;
    g_routineStep = 0U;
    g_routineResult = ROUTINE_RESULT_OK;

    Steering_SetKey(STEERING_KEY_LEFT);
}

static void UdsDiag_HandleRoutineControl(const uint8* uds, uint8 len)
{
    uint16 rid;

    if (len != 4U)
    {
        UdsDiag_SendNegativeResponse(SID_ROUTINE_CONTROL,
                                     NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    if (uds[1] != 0x01U)
    {
        UdsDiag_SendNegativeResponse(SID_ROUTINE_CONTROL,
                                     NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    if (g_diagSession != SESSION_EXTENDED)
    {
        UdsDiag_SendNegativeResponse(SID_ROUTINE_CONTROL,
                                     NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    if (g_routineState != UDS_ROUTINE_NONE)
    {
        UdsDiag_SendNegativeResponse(SID_ROUTINE_CONTROL,
                                     NRC_BUSY_REPEAT_REQUEST);
        return;
    }

    rid = ((uint16)uds[2] << 8U) | (uint16)uds[3];

    switch (rid)
    {
        case ROUTINE_MOTOR_TEST:
            UdsDiag_StartMotorRoutine();
            break;

        case ROUTINE_SERVO_TEST:
            UdsDiag_StartServoRoutine();
            break;

        default:
            UdsDiag_SendNegativeResponse(SID_ROUTINE_CONTROL,
                                         NRC_REQUEST_OUT_OF_RANGE);
            break;
    }
}

static void UdsDiag_SendRoutineResult(uint16 rid, uint8 result)
{
    uint8 payload[5];

    /*
     * 5 UDS bytes:
     * 71 01 RID_H RID_L result
     */
    payload[0] = SID_ROUTINE_CONTROL + POS_RESP_OFFSET;
    payload[1] = 0x01U;
    payload[2] = (uint8)((rid >> 8U) & 0xFFU);
    payload[3] = (uint8)(rid & 0xFFU);
    payload[4] = result;

    UdsDiag_SendSingleFrame(payload, 5U);
}

/* ======================================================
 * Request parser
 * ====================================================== */
static void UdsDiag_ProcessRequest(const uint8 data[8])
{
    uint8 uds[7];
    uint8 len;
    uint8 i;
    uint8 sid;

    /*
     * Only ISO-TP Single Frame:
     * Byte0 high nibble must be 0.
     */
    if ((data[0] & 0xF0U) != 0U)
    {
        return;
    }

    len = data[0] & 0x0FU;

    if ((len == 0U) || (len > 7U))
    {
        return;
    }

    for (i = 0U; i < len; i++)
    {
        uds[i] = data[i + 1U];
    }

    sid = uds[0];
    g_debugUdsLastSid = sid;

    switch (sid)
    {
        case SID_DIAGNOSTIC_SESSION_CONTROL:
            UdsDiag_HandleSessionControl(uds, len);
            break;

        case SID_READ_DATA_BY_IDENTIFIER:
            UdsDiag_HandleReadDID(uds, len);
            break;

        case SID_READ_DTC_INFORMATION:
            UdsDiag_HandleReadDTC(uds, len);
            break;

        case SID_CLEAR_DIAGNOSTIC_INFORMATION:
            UdsDiag_HandleClearDTC(uds, len);
            break;

        case SID_ROUTINE_CONTROL:
            UdsDiag_HandleRoutineControl(uds, len);
            break;

        default:
            UdsDiag_SendNegativeResponse(sid, NRC_SERVICE_NOT_SUPPORTED);
            break;
    }
}

/* ======================================================
 * DTC Monitor
 * ====================================================== */
static void UdsDiag_UpdateOneDtcStatus(uint8* status,
                                       uint8* consecutiveFailCount,
                                       boolean faultActive)
{
    if (faultActive == TRUE)
    {
        *status |= DTC_STATUS_CURRENT_BIT;

        if (*consecutiveFailCount < DTC_CONFIRM_FAIL_COUNT)
        {
            (*consecutiveFailCount)++;
        }

        if (*consecutiveFailCount >= DTC_CONFIRM_FAIL_COUNT)
        {
            *status |= DTC_STATUS_HISTORY_BIT;
        }
    }
    else
    {
        *status = (uint8)(*status & (uint8)(~DTC_STATUS_CURRENT_BIT));
        *consecutiveFailCount = 0U;
    }
}

static void UdsDiag_UpdateDtcMonitor1ms(void)
{
    MotorState motorState;
    uint32 pulsePerSecond;
    sint16 targetAngle;
    sint16 actualAngle;
    uint32 diff;
    boolean encoderFaultActive;
    boolean steeringFaultActive;

    /*
     * DTC 0xC10000
     * 엔코더 신호 없음:
     * 모터 ON 상태에서 2초간 pulse=0
     */
    motorState = MotorControl_GetState();
    pulsePerSecond = Encoder_GetPulsePerSecond();

    if (((motorState == MOTOR_STATE_FORWARD) ||
         (motorState == MOTOR_STATE_REVERSE)) &&
        (pulsePerSecond == 0U))
    {
        if (g_dtcEncoderNoSignalTimerMs < DTC_ENCODER_NO_SIGNAL_MS)
        {
            g_dtcEncoderNoSignalTimerMs++;
        }
    }
    else
    {
        g_dtcEncoderNoSignalTimerMs = 0U;
    }

    /*
     * DTC 0xC10100
     * 조향 명령-실측 불일치:
     * 명령 각도와 실측 각도 차이 > 10도가 1초 이상 지속
     */
    targetAngle = UdsDiag_GetCommandTargetAngleDeg();
    actualAngle = UdsDiag_GetSteeringAngleDeg();
    diff = UdsDiag_AbsDiffSint16(targetAngle, actualAngle);

    if (diff > 10U)
    {
        if (g_dtcSteeringMismatchTimerMs < DTC_STEERING_MISMATCH_MS)
        {
            g_dtcSteeringMismatchTimerMs++;
        }
    }
    else
    {
        g_dtcSteeringMismatchTimerMs = 0U;
    }

    g_dtcMonitorPeriodTimerMs++;
    if (g_dtcMonitorPeriodTimerMs >= DTC_MONITOR_PERIOD_MS)
    {
        g_dtcMonitorPeriodTimerMs = 0U;

        encoderFaultActive =
            (g_dtcEncoderNoSignalTimerMs >= DTC_ENCODER_NO_SIGNAL_MS) ? TRUE : FALSE;
        steeringFaultActive =
            (g_dtcSteeringMismatchTimerMs >= DTC_STEERING_MISMATCH_MS) ? TRUE : FALSE;

        UdsDiag_UpdateOneDtcStatus(&g_dtcC100Status,
                                   &g_dtcC100ConsecutiveFailCount,
                                   encoderFaultActive);
        UdsDiag_UpdateOneDtcStatus(&g_dtcC101Status,
                                   &g_dtcC101ConsecutiveFailCount,
                                   steeringFaultActive);
    }

    g_debugUdsDtcC100 = g_dtcC100Status;
    g_debugUdsDtcC101 = g_dtcC101Status;
}

/* ======================================================
 * Routine Update
 * ====================================================== */
static void UdsDiag_UpdateMotorRoutine1ms(void)
{
    g_routineTimerMs++;

    /*
     * 1초 동안 PWM 30% 전진
     */
    MotorControl_SetDutyPercent(30U);
    MotorControl_Forward();

    if (g_routineTimerMs >= 1000U)
    {
        MotorControl_Stop();
        MotorControl_SetDutyPercent(g_savedMotorDuty);

        g_routineResult = ROUTINE_RESULT_OK;
        UdsDiag_SendRoutineResult(ROUTINE_MOTOR_TEST, ROUTINE_RESULT_OK);

        g_routineState = UDS_ROUTINE_NONE;
        g_routineTimerMs = 0U;
        g_routineStep = 0U;
    }
}

static void UdsDiag_UpdateServoRoutine1ms(void)
{
    g_routineTimerMs++;

    /*
     * Output LEFT -> CENTER -> RIGHT, 800 ms at each position.
     * Return to CENTER after sending the routine result.
     */
    switch (g_routineStep)
    {
        case 0U:
            Steering_SetKey(STEERING_KEY_LEFT);
            break;

        case 1U:
            Steering_SetKey(STEERING_KEY_NULL);
            break;

        case 2U:
            Steering_SetKey(STEERING_KEY_RIGHT);
            break;

        default:
            Steering_SetKey(STEERING_KEY_RIGHT);
            break;
    }

    if (g_routineTimerMs < 800U)
    {
        return;
    }

    g_routineTimerMs = 0U;
    g_routineStep++;

    if (g_routineStep >= 3U)
    {
        Steering_SetKey(STEERING_KEY_NULL);

        g_routineResult = ROUTINE_RESULT_OK;
        UdsDiag_SendRoutineResult(ROUTINE_SERVO_TEST, ROUTINE_RESULT_OK);

        g_routineState = UDS_ROUTINE_NONE;
        g_routineTimerMs = 0U;
        g_routineStep = 0U;
    }
}

static void UdsDiag_UpdateRoutine1ms(void)
{
    switch (g_routineState)
    {
        case UDS_ROUTINE_MOTOR_TEST:
            UdsDiag_UpdateMotorRoutine1ms();
            break;

        case UDS_ROUTINE_SERVO_TEST:
            UdsDiag_UpdateServoRoutine1ms();
            break;

        case UDS_ROUTINE_NONE:
        default:
            break;
    }

    g_debugUdsRoutineState = (uint32)g_routineState;
    g_debugUdsRoutineResult = (uint32)g_routineResult;
}

/* ======================================================
 * Public
 * ====================================================== */
void UdsDiag_Init(void)
{
    uint8 i;

    g_diagSession = SESSION_DEFAULT;

    g_dtcC100Status = DTC_STATUS_INIT;
    g_dtcC101Status = DTC_STATUS_INIT;
    g_dtcEncoderNoSignalTimerMs = 0U;
    g_dtcSteeringMismatchTimerMs = 0U;
    g_dtcMonitorPeriodTimerMs = 0U;
    g_dtcC100ConsecutiveFailCount = 0U;
    g_dtcC101ConsecutiveFailCount = 0U;

    g_routineState = UDS_ROUTINE_NONE;
    g_routineTimerMs = 0U;
    g_routineStep = 0U;
    g_routineResult = ROUTINE_RESULT_OK;

    g_reqPending = FALSE;

    for (i = 0U; i < 8U; i++)
    {
        g_reqData[i] = 0U;
    }

    g_udsTxBufferIndex = UDS_TX_BUFFER_0;

    g_debugUdsRxCount = 0U;
    g_debugUdsTxCount = 0U;
    g_debugUdsTxBusyCount = 0U;
    g_debugUdsLastSid = 0U;
    g_debugUdsLastNrc = 0U;
    g_debugUdsDtcC100 = 0U;
    g_debugUdsDtcC101 = 0U;
    g_debugUdsRoutineState = 0U;
    g_debugUdsRoutineResult = 0U;
}

void UdsDiag_OnCanRequest(const uint8 data[8])
{
    uint8 i;

    for (i = 0U; i < 8U; i++)
    {
        g_reqData[i] = data[i];
    }

    g_reqPending = TRUE;
    g_debugUdsRxCount++;
}

void UdsDiag_Update1ms(void)
{
    uint8 localReq[8];
    uint8 i;

    /*
     * Request 처리
     */
    if (g_reqPending == TRUE)
    {
        for (i = 0U; i < 8U; i++)
        {
            localReq[i] = g_reqData[i];
        }

        g_reqPending = FALSE;

        UdsDiag_ProcessRequest(localReq);
    }

    /*
     * Routine은 main loop에서 상태머신으로 실행.
     */
    UdsDiag_UpdateRoutine1ms();

    /*
     * DTC monitor.
     */
    UdsDiag_UpdateDtcMonitor1ms();
}
