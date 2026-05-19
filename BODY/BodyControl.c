#include "BodyControl.h"

#include "Ifx_Types.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "Bsp.h"

/*
 * ======================================================
 * BODY ECU Pin Mapping
 *
 * RGB Headlamp:
 *   R = P33.12
 *   G = P32.4
 *   B = P23.0
 *
 * Brake Lamp:
 *   SCL = P13.1
 *
 * Turn Signal:
 *   Left  = D7 = P02.4
 *   Right = D8 = P02.6
 *
 * Buzzer:
 *   Easy Module Shield Buzzer = D5 = P02.3
 * ======================================================
 */

#define BODY_LED_ACTIVE_HIGH          (1U)
#define BODY_RGB_ACTIVE_HIGH          (1U)
#define BODY_BRAKE_ACTIVE_HIGH        (1U)
#define BODY_BUZZER_ACTIVE_HIGH       (1U)

#define BODY_BLINK_HALF_PERIOD_MS     (500U)

/* =========================
 * RGB Headlamp Pins
 * ========================= */

#define BODY_HEAD_R_PORT              (&MODULE_P33)
#define BODY_HEAD_R_PIN               (12U)

#define BODY_HEAD_G_PORT              (&MODULE_P32)
#define BODY_HEAD_G_PIN               (4U)

#define BODY_HEAD_B_PORT              (&MODULE_P23)
#define BODY_HEAD_B_PIN               (0U)

/* =========================
 * Brake Lamp Pin
 * ========================= */

#define BODY_BRAKE_PORT               (&MODULE_P13)
#define BODY_BRAKE_PIN                (1U)

/* =========================
 * Turn Signal Pins
 * ========================= */

#define BODY_LEFT_TURN_PORT           (&MODULE_P02)
#define BODY_LEFT_TURN_PIN            (4U)

#define BODY_RIGHT_TURN_PORT          (&MODULE_P02)
#define BODY_RIGHT_TURN_PIN           (6U)

/* =========================
 * Buzzer Pin
 * ========================= */

/* Easy Module Shield Buzzer = D5 = P02.3 */
#define BODY_BUZZER_PORT              (&MODULE_P02)
#define BODY_BUZZER_PIN               (3U)

/* =========================
 * Internal State
 * ========================= */

static boolean g_headlampOn = FALSE;
static boolean g_brakeLampOn = FALSE;

static boolean g_leftTurnEnabled = FALSE;
static boolean g_rightTurnEnabled = FALSE;

static boolean g_blinkOutputOn = FALSE;
static uint32 g_blinkTimerMs = 0U;

/* =========================
 * Delay
 * ========================= */

static void BodyControl_DelayUs(uint32 us)
{
    waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, us));
}

static void BodyControl_DelayMs(uint32 ms)
{
    uint32 i;

    for (i = 0U; i < ms; i++)
    {
        BodyControl_DelayUs(1000U);
    }
}

static void BodyControl_Delay1ms(void)
{
    BodyControl_DelayUs(1000U);
}

/* =========================
 * GPIO Low Level
 * ========================= */

static void BodyControl_InitOutputPin(Ifx_P* port, uint8 pin)
{
    IfxPort_setPinMode(port,
                       pin,
                       IfxPort_Mode_outputPushPullGeneral);

    IfxPort_setPinPadDriver(port,
                            pin,
                            IfxPort_PadDriver_cmosAutomotiveSpeed4);

    IfxPort_setPinLow(port, pin);
}

static void BodyControl_WriteNormalLedPin(Ifx_P* port, uint8 pin, boolean on)
{
#if BODY_LED_ACTIVE_HIGH
    if (on != FALSE)
    {
        IfxPort_setPinHigh(port, pin);
    }
    else
    {
        IfxPort_setPinLow(port, pin);
    }
#else
    if (on != FALSE)
    {
        IfxPort_setPinLow(port, pin);
    }
    else
    {
        IfxPort_setPinHigh(port, pin);
    }
#endif
}

static void BodyControl_WriteRgbPin(Ifx_P* port, uint8 pin, boolean on)
{
#if BODY_RGB_ACTIVE_HIGH
    if (on != FALSE)
    {
        IfxPort_setPinHigh(port, pin);
    }
    else
    {
        IfxPort_setPinLow(port, pin);
    }
#else
    if (on != FALSE)
    {
        IfxPort_setPinLow(port, pin);
    }
    else
    {
        IfxPort_setPinHigh(port, pin);
    }
#endif
}

static void BodyControl_WriteBrakePin(boolean on)
{
#if BODY_BRAKE_ACTIVE_HIGH
    if (on != FALSE)
    {
        IfxPort_setPinHigh(BODY_BRAKE_PORT, BODY_BRAKE_PIN);
    }
    else
    {
        IfxPort_setPinLow(BODY_BRAKE_PORT, BODY_BRAKE_PIN);
    }
#else
    if (on != FALSE)
    {
        IfxPort_setPinLow(BODY_BRAKE_PORT, BODY_BRAKE_PIN);
    }
    else
    {
        IfxPort_setPinHigh(BODY_BRAKE_PORT, BODY_BRAKE_PIN);
    }
#endif
}

static void BodyControl_WriteBuzzerPin(boolean on)
{
#if BODY_BUZZER_ACTIVE_HIGH
    if (on != FALSE)
    {
        IfxPort_setPinHigh(BODY_BUZZER_PORT, BODY_BUZZER_PIN);
    }
    else
    {
        IfxPort_setPinLow(BODY_BUZZER_PORT, BODY_BUZZER_PIN);
    }
#else
    if (on != FALSE)
    {
        IfxPort_setPinLow(BODY_BUZZER_PORT, BODY_BUZZER_PIN);
    }
    else
    {
        IfxPort_setPinHigh(BODY_BUZZER_PORT, BODY_BUZZER_PIN);
    }
#endif
}

/* =========================
 * Buzzer Sound
 * ========================= */

static void BodyControl_BuzzerOff(void)
{
    BodyControl_WriteBuzzerPin(FALSE);
}

/*
 * Passive buzzer tone.
 * freqHz 예:
 * 400~500Hz: 경적 느낌
 * 1000~1500Hz: 삐삐 경고음 느낌
 */
static void BodyControl_PlayToneForMs(uint32 freqHz, uint32 durationMs)
{
    uint32 halfPeriodUs;
    uint32 elapsedUs;
    uint32 durationUs;

    if (freqHz == 0U)
    {
        BodyControl_BuzzerOff();
        BodyControl_DelayMs(durationMs);
        return;
    }

    halfPeriodUs = 500000U / freqHz;

    if (halfPeriodUs == 0U)
    {
        halfPeriodUs = 1U;
    }

    durationUs = durationMs * 1000U;
    elapsedUs = 0U;

    while (elapsedUs < durationUs)
    {
        BodyControl_WriteBuzzerPin(TRUE);
        BodyControl_DelayUs(halfPeriodUs);

        BodyControl_WriteBuzzerPin(FALSE);
        BodyControl_DelayUs(halfPeriodUs);

        elapsedUs += (halfPeriodUs * 2U);
    }

    BodyControl_BuzzerOff();
}

/*
 * 무음 대기 중에도 방향지시등 깜빡임은 유지
 */
static void BodyControl_SilentForMs(uint32 durationMs)
{
    uint32 i;

    BodyControl_BuzzerOff();

    for (i = 0U; i < durationMs; i++)
    {
        BodyControl_Update1ms();
        BodyControl_Delay1ms();
    }
}

/*
 * 경적다운 소리:
 * - 끊어지는 삐삐음 X
 * - 낮은 톤을 길게 유지
 * - 아주 짧게 주파수를 흔들어서 "빠아앙" 느낌
 *
 * 부저 하나로 실제 자동차 듀얼 혼을 완벽히 만들 수는 없지만,
 * 기존 삐용삐용보다 훨씬 경적에 가까움.
 */
void BodyControl_HornForMs(uint32 holdTimeMs)
{
    uint32 elapsedMs;
    uint32 sliceMs;

    elapsedMs = 0U;

    /*
     * 시작 어택: 살짝 높은 소리로 짧게 시작
     * "빡" 하는 느낌
     */
    if (holdTimeMs > 80U)
    {
        BodyControl_PlayToneForMs(520U, 80U);
        elapsedMs += 80U;
    }

    /*
     * 본 경적음:
     * 390Hz / 430Hz를 아주 짧게 번갈아 울림.
     * 사람 귀에는 하나의 거친 "빠아앙" 경적처럼 들림.
     */
    while (elapsedMs < holdTimeMs)
    {
        sliceMs = 35U;

        if ((elapsedMs + sliceMs) > holdTimeMs)
        {
            sliceMs = holdTimeMs - elapsedMs;
        }

        BodyControl_PlayToneForMs(390U, sliceMs);
        elapsedMs += sliceMs;

        if (elapsedMs >= holdTimeMs)
        {
            break;
        }

        sliceMs = 35U;

        if ((elapsedMs + sliceMs) > holdTimeMs)
        {
            sliceMs = holdTimeMs - elapsedMs;
        }

        BodyControl_PlayToneForMs(430U, sliceMs);
        elapsedMs += sliceMs;
    }

    BodyControl_BuzzerOff();
}

/*
 * 충돌 방지 경고음
 *
 * distanceCm >= 20 : 무음
 * distanceCm < 20  : 1단계, 느린 삐삐삐삐
 * distanceCm < 10  : 2단계, 빠른 삐삐삐삐
 * distanceCm < 5   : 3단계, 연속 삐---
 *
 * 1단계/2단계/3단계 모두 같은 음 높이 사용.
 * 차이는 삐삐 간격만 다르게 함.
 */
void BodyControl_CollisionWarningForMs(uint16 distanceCm, uint32 holdTimeMs)
{
    uint32 elapsedMs;

    /*
     * 경고음 음 높이
     * 1단계, 2단계, 3단계 모두 같은 주파수 사용
     */
    const uint32 warningFreqHz = 1300U;

    elapsedMs = 0U;

    /*
     * 충분히 멈: 소리 없음
     */
    if (distanceCm >= 20U)
    {
        BodyControl_SilentForMs(holdTimeMs);
        return;
    }

    /*
     * 3단계: 5cm 미만
     * 연속 삐---
     */
    if (distanceCm < 5U)
    {
        BodyControl_PlayToneForMs(warningFreqHz, holdTimeMs);
        BodyControl_BuzzerOff();
        return;
    }

    /*
     * 2단계: 10cm 미만
     * 빠른 삐삐삐삐
     *
     * ON 120ms / OFF 130ms
     * 대략 0.25초마다 한 번씩 울림
     */
    if (distanceCm < 10U)
    {
        while (elapsedMs < holdTimeMs)
        {
            BodyControl_PlayToneForMs(warningFreqHz, 120U);
            elapsedMs += 120U;

            if (elapsedMs < holdTimeMs)
            {
                BodyControl_SilentForMs(130U);
                elapsedMs += 130U;
            }
        }

        BodyControl_BuzzerOff();
        return;
    }

    /*
     * 1단계: 20cm 미만
     * 느린 삐삐삐삐
     *
     * ON 120ms / OFF 380ms
     * 대략 0.5초마다 한 번씩 울림
     */
    while (elapsedMs < holdTimeMs)
    {
        BodyControl_PlayToneForMs(warningFreqHz, 120U);
        elapsedMs += 120U;

        if (elapsedMs < holdTimeMs)
        {
            BodyControl_SilentForMs(380U);
            elapsedMs += 380U;
        }
    }

    BodyControl_BuzzerOff();
}
/* =========================
 * Output Apply
 * ========================= */

static void BodyControl_WriteHeadlamp(boolean on)
{
    /*
     * 흰색 전조등 = R + G + B 전부 ON
     */
    BodyControl_WriteRgbPin(BODY_HEAD_R_PORT,
                            BODY_HEAD_R_PIN,
                            on);

    BodyControl_WriteRgbPin(BODY_HEAD_G_PORT,
                            BODY_HEAD_G_PIN,
                            on);

    BodyControl_WriteRgbPin(BODY_HEAD_B_PORT,
                            BODY_HEAD_B_PIN,
                            on);
}

static void BodyControl_ApplyOutputs(void)
{
    boolean leftLedOn;
    boolean rightLedOn;

    BodyControl_WriteHeadlamp(g_headlampOn);
    BodyControl_WriteBrakePin(g_brakeLampOn);

    leftLedOn = ((g_leftTurnEnabled != FALSE) &&
                 (g_blinkOutputOn != FALSE)) ? TRUE : FALSE;

    rightLedOn = ((g_rightTurnEnabled != FALSE) &&
                  (g_blinkOutputOn != FALSE)) ? TRUE : FALSE;

    BodyControl_WriteNormalLedPin(BODY_LEFT_TURN_PORT,
                                  BODY_LEFT_TURN_PIN,
                                  leftLedOn);

    BodyControl_WriteNormalLedPin(BODY_RIGHT_TURN_PORT,
                                  BODY_RIGHT_TURN_PIN,
                                  rightLedOn);
}

/* =========================
 * Public Functions
 * ========================= */

void BodyControl_Init(void)
{
    /*
     * RGB Headlamp
     */
    BodyControl_InitOutputPin(BODY_HEAD_R_PORT,
                              BODY_HEAD_R_PIN);

    BodyControl_InitOutputPin(BODY_HEAD_G_PORT,
                              BODY_HEAD_G_PIN);

    BodyControl_InitOutputPin(BODY_HEAD_B_PORT,
                              BODY_HEAD_B_PIN);

    /*
     * Brake Lamp
     */
    BodyControl_InitOutputPin(BODY_BRAKE_PORT,
                              BODY_BRAKE_PIN);

    /*
     * Turn Signal
     */
    BodyControl_InitOutputPin(BODY_LEFT_TURN_PORT,
                              BODY_LEFT_TURN_PIN);

    BodyControl_InitOutputPin(BODY_RIGHT_TURN_PORT,
                              BODY_RIGHT_TURN_PIN);

    /*
     * Buzzer
     */
    BodyControl_InitOutputPin(BODY_BUZZER_PORT,
                              BODY_BUZZER_PIN);

    g_headlampOn = FALSE;
    g_brakeLampOn = FALSE;

    g_leftTurnEnabled = FALSE;
    g_rightTurnEnabled = FALSE;

    g_blinkOutputOn = FALSE;
    g_blinkTimerMs = 0U;

    BodyControl_BuzzerOff();
    BodyControl_ApplyOutputs();
}

/* Headlamp */

void BodyControl_SetHeadlamp(boolean on)
{
    g_headlampOn = on;
    BodyControl_ApplyOutputs();
}

void BodyControl_HeadlampOn(void)
{
    BodyControl_SetHeadlamp(TRUE);
}

void BodyControl_HeadlampOff(void)
{
    BodyControl_SetHeadlamp(FALSE);
}

/* Brake Lamp */

void BodyControl_SetBrakeLamp(boolean on)
{
    g_brakeLampOn = on;
    BodyControl_ApplyOutputs();
}

void BodyControl_BrakeLampOn(void)
{
    BodyControl_SetBrakeLamp(TRUE);
}

void BodyControl_BrakeLampOff(void)
{
    BodyControl_SetBrakeLamp(FALSE);
}

/* Left Turn */

void BodyControl_SetLeftTurn(boolean on)
{
    g_leftTurnEnabled = on;

    if (on != FALSE)
    {
        g_blinkOutputOn = TRUE;
        g_blinkTimerMs = 0U;
    }

    BodyControl_ApplyOutputs();
}

void BodyControl_LeftTurnOn(void)
{
    BodyControl_SetLeftTurn(TRUE);
}

void BodyControl_LeftTurnOff(void)
{
    BodyControl_SetLeftTurn(FALSE);
}

/* Right Turn */

void BodyControl_SetRightTurn(boolean on)
{
    g_rightTurnEnabled = on;

    if (on != FALSE)
    {
        g_blinkOutputOn = TRUE;
        g_blinkTimerMs = 0U;
    }

    BodyControl_ApplyOutputs();
}

void BodyControl_RightTurnOn(void)
{
    BodyControl_SetRightTurn(TRUE);
}

void BodyControl_RightTurnOff(void)
{
    BodyControl_SetRightTurn(FALSE);
}

/* All Off */

void BodyControl_AllOff(void)
{
    g_headlampOn = FALSE;
    g_brakeLampOn = FALSE;

    g_leftTurnEnabled = FALSE;
    g_rightTurnEnabled = FALSE;

    g_blinkOutputOn = FALSE;
    g_blinkTimerMs = 0U;

    BodyControl_BuzzerOff();
    BodyControl_ApplyOutputs();
}

/* Update */

void BodyControl_Update1ms(void)
{
    if ((g_leftTurnEnabled == FALSE) &&
        (g_rightTurnEnabled == FALSE))
    {
        g_blinkOutputOn = FALSE;
        g_blinkTimerMs = 0U;
        BodyControl_ApplyOutputs();
        return;
    }

    g_blinkTimerMs++;

    if (g_blinkTimerMs >= BODY_BLINK_HALF_PERIOD_MS)
    {
        g_blinkTimerMs = 0U;

        if (g_blinkOutputOn == FALSE)
        {
            g_blinkOutputOn = TRUE;
        }
        else
        {
            g_blinkOutputOn = FALSE;
        }

        BodyControl_ApplyOutputs();
    }
}

void BodyControl_UpdateForMs(uint32 holdTimeMs)
{
    uint32 i;

    for (i = 0U; i < holdTimeMs; i++)
    {
        BodyControl_Update1ms();
        BodyControl_Delay1ms();
    }
}
