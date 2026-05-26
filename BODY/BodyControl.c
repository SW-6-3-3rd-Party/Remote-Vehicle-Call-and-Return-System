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
 *   R = P33.7
 *   G = P33.5
 *   B = P33.3
 *
 * Brake Lamp:
 *   SCL = P13.1
 *
 * Turn Signal:
 *   Left  = D8 = P02.6
 *   Right = D7 = P02.4
 *
 * Buzzer:
 *   Easy Module Shield Buzzer = D5 = P02.3
 *
 * Ultrasonic sensor on Easy Module Shield serial connector:
 *   ECHO = TXD = D1 = P15.2
 *   TRIG = RXD = D0 = P15.3
 *
 * Diagnosis feedback input:
 *   Brake/Rear Lamp Feedback = P00.0
 *   Headlamp Feedback        = P00.2
 *   Left Turn Feedback       = P00.6
 *   Right Turn Feedback      = P00.8
 *
 * Collision event button:
 *   Button = P33.10
 *   Default wiring assumption: P33.10 -- button -- 3.3V
 *   Internal pull-down is enabled, pressed = HIGH.
 * ======================================================
 */

#define BODY_LED_ACTIVE_HIGH          (1U)
#define BODY_RGB_ACTIVE_HIGH          (1U)
#define BODY_BRAKE_ACTIVE_HIGH        (1U)
#define BODY_BUZZER_ACTIVE_HIGH       (1U)

#define BODY_BLINK_HALF_PERIOD_MS     (500U)

#define BODY_COLLISION_WARNING_STAGE1_CM      (30U)
#define BODY_COLLISION_WARNING_STAGE2_CM      (20U)
#define BODY_COLLISION_WARNING_STAGE3_CM      (10U)
#define BODY_COLLISION_WARNING_HYSTERESIS_CM  (5U)

#define BODY_COLLISION_WARNING_BEEP_ON_MS     (120U)
#define BODY_COLLISION_WARNING_STAGE1_CYCLE_MS (500U)
#define BODY_COLLISION_WARNING_STAGE2_CYCLE_MS (250U)
#define BODY_COLLISION_WARNING_TONE_HALF_MS   (1U)

#define BODY_ULTRASONIC_MEASURE_PERIOD_MS     (60U)
#define BODY_ULTRASONIC_VALID_HOLD_MS         (300U)
#define BODY_ULTRASONIC_ECHO_TIMEOUT_US       (3000U)
#define BODY_ULTRASONIC_ECHO_IDLE_TIMEOUT_US  (300U)
#define BODY_ULTRASONIC_NO_OBJECT_CM          (255U)

/* =========================
 * RGB Headlamp Pins
 * ========================= */

#define BODY_HEAD_R_PORT              (&MODULE_P33)
#define BODY_HEAD_R_PIN               (7U)

#define BODY_HEAD_G_PORT              (&MODULE_P33)
#define BODY_HEAD_G_PIN               (5U)

#define BODY_HEAD_B_PORT              (&MODULE_P33)
#define BODY_HEAD_B_PIN               (3U)

/* =========================
 * Brake Lamp Pin
 * ========================= */

#define BODY_BRAKE_PORT               (&MODULE_P13)
#define BODY_BRAKE_PIN                (1U)

/* =========================
 * Turn Signal Pins
 * ========================= */

#define BODY_LEFT_TURN_PORT           (&MODULE_P02)
#define BODY_LEFT_TURN_PIN            (6U)

#define BODY_RIGHT_TURN_PORT          (&MODULE_P02)
#define BODY_RIGHT_TURN_PIN           (4U)

/* =========================
 * Buzzer Pin
 * ========================= */

/* Easy Module Shield Buzzer = D5 = P02.3 */
#define BODY_BUZZER_PORT              (&MODULE_P02)
#define BODY_BUZZER_PIN               (3U)

/* =========================
 * Ultrasonic Sensor Pins
 * ========================= */

/* Easy Module Shield serial TXD/RXD connector used as GPIO. */
#define BODY_ULTRASONIC_ECHO_PORT     (&MODULE_P15)
#define BODY_ULTRASONIC_ECHO_PIN      (2U)

#define BODY_ULTRASONIC_TRIG_PORT     (&MODULE_P15)
#define BODY_ULTRASONIC_TRIG_PIN      (3U)


/* =========================
 * Lamp Diagnosis Feedback Pins
 * =========================
 *
 * 진단 방식:
 * - BODY ECU가 램프 ON 명령을 출력했는데
 * - 아래 feedback input pin에서 HIGH가 일정 시간 동안 한 번도 안 보이면
 * - 해당 램프 고장으로 latch 한다.
 *
 * 주의:
 * - TC375 GPIO는 3.3V 기준이다. 5V를 직접 넣지 말 것.
 * - 진단 입력은 Pull-Down으로 둔다. 선이 빠지면 LOW로 읽혀 고장 판정된다.
 */
#define BODY_DIAG_ACTIVE_HIGH          (1U)

#define BODY_DIAG_BRAKE_FB_PORT       (&MODULE_P00)
#define BODY_DIAG_BRAKE_FB_PIN        (0U)

#define BODY_DIAG_HEAD_FB_PORT        (&MODULE_P00)
#define BODY_DIAG_HEAD_FB_PIN         (2U)

#define BODY_DIAG_LEFT_FB_PORT        (&MODULE_P00)
#define BODY_DIAG_LEFT_FB_PIN         (6U)

#define BODY_DIAG_RIGHT_FB_PORT       (&MODULE_P00)
#define BODY_DIAG_RIGHT_FB_PIN        (8U)

/* Diagnosis danger indicator LED: ON when any diagnostic fault is latched. */
#define BODY_DIAG_DANGER_LED_PORT     (&MODULE_P00)
#define BODY_DIAG_DANGER_LED_PIN      (10U)

/*
 * Headlamp/Brake는 계속 켜져 있어야 하므로 100ms 안에 feedback HIGH가 안 보이면 고장.
 * Turn signal은 점멸이므로 최소 한 번 HIGH를 볼 시간을 주기 위해 1200ms로 둔다.
 */
#define BODY_DIAG_STEADY_CONFIRM_MS   (100U)
#define BODY_DIAG_BLINK_CONFIRM_MS    (1200U)
#define BODY_DIAG_STARTUP_SELF_TEST_MS (1000U)

/* =========================
 * Collision Event Button
 * =========================
 * 기본 배선:
 *   P33.10 -- 버튼 -- GND
 *   내부 Pull-Up 사용
 *   안 누름 = HIGH, 누름 = LOW
 *
 * 만약 버튼을 3.3V 쪽으로 연결하고 외부 Pull-Down을 쓴다면
 * BODY_COLLISION_BUTTON_ACTIVE_LOW를 0U로 바꿔라.
 */
/*
 * Active-high button configuration is selected below:
 * P33.10 is pulled down internally and a press drives the pin HIGH.
 */
#define BODY_COLLISION_BUTTON_PORT             (&MODULE_P33)
#define BODY_COLLISION_BUTTON_PIN              (10U)
#define BODY_COLLISION_BUTTON_ACTIVE_LOW       (0U)
#define BODY_COLLISION_BUTTON_DEBOUNCE_MS      (20U)

/* =========================
 * Internal State
 * ========================= */

static boolean g_headlampOn = FALSE;
static boolean g_brakeLampOn = FALSE;

static boolean g_leftTurnEnabled = FALSE;
static boolean g_rightTurnEnabled = FALSE;

/* CAN Byte4 collision warning enable. This controls ultrasonic warning sound only. */
static boolean g_collisionWarningLampOn = FALSE;

/* CAN 경적용 non-blocking buzzer state */
static boolean g_hornEnabled = FALSE;
static boolean g_hornOutputOn = FALSE;
static uint32 g_hornTimerMs = 0U;

/* Ultrasonic collision warning sound state. Horn command has buzzer priority. */
static uint16 g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
static boolean g_ultrasonicDistanceValid = FALSE;
static uint32 g_ultrasonicMeasureTimerMs = 0U;
static uint32 g_ultrasonicValidHoldTimerMs = 0U;
static uint32 g_ultrasonicMeasureCount = 0U;
static uint32 g_ultrasonicTimeoutCount = 0U;

static uint8 g_collisionWarningLevel = 0U;
static uint8 g_collisionWarningLastLevel = 0U;
static boolean g_collisionWarningToneOutputOn = FALSE;
static uint32 g_collisionWarningToneTimerMs = 0U;
static uint32 g_collisionWarningPatternTimerMs = 0U;

static boolean g_blinkOutputOn = FALSE;
static uint32 g_blinkTimerMs = 0U;

/* Collision button debounce/event state */
static boolean g_collisionButtonLastRawPressed = FALSE;
static boolean g_collisionButtonStablePressed = FALSE;
static boolean g_collisionButtonEventPending = FALSE;
static uint32 g_collisionButtonDebounceTimerMs = 0U;
static uint32 g_collisionButtonPressedEventCount = 0U;

/* Lamp diagnosis state */
static uint8 g_diagFaultMask = 0U;
static uint8 g_diagCommandMask = 0U;
static uint8 g_diagFeedbackMask = 0U;

static uint32 g_diagBrakeLowTimerMs = 0U;
static uint32 g_diagHeadLowTimerMs = 0U;
static uint32 g_diagLeftLowTimerMs = 0U;
static uint32 g_diagRightLowTimerMs = 0U;

volatile uint8 g_debugBodyDiagFaultMask = 0U;
volatile uint8 g_debugBodyDiagCommandMask = 0U;
volatile uint8 g_debugBodyDiagFeedbackMask = 0U;
volatile uint8 g_debugBodyDiagStartupFaultMask = 0U;

volatile uint8 g_debugBodyCollisionButtonRawPressed = 0U;
volatile uint8 g_debugBodyCollisionButtonStablePressed = 0U;
volatile uint32 g_debugBodyCollisionButtonEventCount = 0U;

volatile uint16 g_debugBodyUltrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
volatile uint8 g_debugBodyUltrasonicDistanceValid = 0U;
volatile uint8 g_debugBodyCollisionWarningLevel = 0U;
volatile uint32 g_debugBodyUltrasonicMeasureCount = 0U;
volatile uint32 g_debugBodyUltrasonicTimeoutCount = 0U;

static void BodyControl_WriteBuzzerPin(boolean on);
static void BodyControl_BuzzerOff(void);
static void BodyControl_UpdateDiagDangerLed(void);
static void BodyControl_WriteNormalLedPin(Ifx_P* port, uint8 pin, boolean on);
static void BodyControl_WriteHeadlamp(boolean on);
static void BodyControl_WriteBrakePin(boolean on);
static void BodyControl_ApplyOutputs(void);

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


static void BodyControl_InitInputPinPulldown(Ifx_P* port, uint8 pin)
{
    IfxPort_setPinMode(port,
                       pin,
                       IfxPort_Mode_inputPullDown);
}

static void BodyControl_InitInputPinPullup(Ifx_P* port, uint8 pin)
{
    IfxPort_setPinMode(port,
                       pin,
                       IfxPort_Mode_inputPullUp);
}

static void BodyControl_WriteUltrasonicTrig(boolean on)
{
    if (on != FALSE)
    {
        IfxPort_setPinHigh(BODY_ULTRASONIC_TRIG_PORT, BODY_ULTRASONIC_TRIG_PIN);
    }
    else
    {
        IfxPort_setPinLow(BODY_ULTRASONIC_TRIG_PORT, BODY_ULTRASONIC_TRIG_PIN);
    }
}

static boolean BodyControl_ReadUltrasonicEchoHigh(void)
{
    return (IfxPort_getPinState(BODY_ULTRASONIC_ECHO_PORT,
                                BODY_ULTRASONIC_ECHO_PIN) != FALSE) ? TRUE : FALSE;
}

static boolean BodyControl_ReadDiagPin(Ifx_P* port, uint8 pin)
{
    boolean isHigh;

    isHigh = (IfxPort_getPinState(port, pin) != FALSE) ? TRUE : FALSE;

#if BODY_DIAG_ACTIVE_HIGH
    return isHigh;
#else
    return (isHigh == FALSE) ? TRUE : FALSE;
#endif
}

static boolean BodyControl_ReadCollisionButtonRawPressed(void)
{
    boolean isHigh;

    isHigh = (IfxPort_getPinState(BODY_COLLISION_BUTTON_PORT,
                                  BODY_COLLISION_BUTTON_PIN) != FALSE) ? TRUE : FALSE;

#if BODY_COLLISION_BUTTON_ACTIVE_LOW
    return (isHigh == FALSE) ? TRUE : FALSE;
#else
    return isHigh;
#endif
}

static void BodyControl_UpdateCollisionButton1ms(void)
{
    boolean rawPressed;

    rawPressed = BodyControl_ReadCollisionButtonRawPressed();

    g_debugBodyCollisionButtonRawPressed = (rawPressed != FALSE) ? 1U : 0U;

    if (rawPressed != g_collisionButtonLastRawPressed)
    {
        g_collisionButtonLastRawPressed = rawPressed;
        g_collisionButtonDebounceTimerMs = 0U;
        return;
    }

    if (g_collisionButtonDebounceTimerMs < BODY_COLLISION_BUTTON_DEBOUNCE_MS)
    {
        g_collisionButtonDebounceTimerMs++;
        return;
    }

    if (rawPressed != g_collisionButtonStablePressed)
    {
        g_collisionButtonStablePressed = rawPressed;

        g_debugBodyCollisionButtonStablePressed =
            (g_collisionButtonStablePressed != FALSE) ? 1U : 0U;

        /* 눌림 edge에서만 충돌 발생 이벤트를 1회 생성한다. */
        if (g_collisionButtonStablePressed != FALSE)
        {
            g_collisionButtonEventPending = TRUE;
            g_collisionButtonPressedEventCount++;
            g_debugBodyCollisionButtonEventCount = g_collisionButtonPressedEventCount;
        }
    }
}

static uint32 BodyControl_GetStmTicks(void)
{
    return IfxStm_getLower(BSP_DEFAULT_TIMER);
}

static uint32 BodyControl_GetTicksFromUs(uint32 us)
{
    uint32 ticksPerUs;

    ticksPerUs = (uint32)IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, 1U);

    if (ticksPerUs == 0U)
    {
        ticksPerUs = 1U;
    }

    return ticksPerUs * us;
}

static boolean BodyControl_WaitUltrasonicEchoState(boolean targetHigh,
                                                   uint32 timeoutUs)
{
    uint32 startTicks;
    uint32 timeoutTicks;
    boolean currentHigh;

    startTicks = BodyControl_GetStmTicks();
    timeoutTicks = BodyControl_GetTicksFromUs(timeoutUs);

    do
    {
        currentHigh = BodyControl_ReadUltrasonicEchoHigh();

        if (currentHigh == targetHigh)
        {
            return TRUE;
        }
    }
    while ((BodyControl_GetStmTicks() - startTicks) < timeoutTicks);

    return FALSE;
}

static boolean BodyControl_MeasureUltrasonicDistanceCm(uint16* distanceCm)
{
    uint32 pulseStartTicks;
    uint32 pulseTicks;
    uint32 timeoutTicks;
    uint32 ticksPerUs;
    uint32 pulseUs;
    uint32 distance;

    if (BodyControl_WaitUltrasonicEchoState(FALSE,
                                            BODY_ULTRASONIC_ECHO_IDLE_TIMEOUT_US) == FALSE)
    {
        return FALSE;
    }

    BodyControl_WriteUltrasonicTrig(FALSE);
    BodyControl_DelayUs(2U);
    BodyControl_WriteUltrasonicTrig(TRUE);
    BodyControl_DelayUs(10U);
    BodyControl_WriteUltrasonicTrig(FALSE);

    if (BodyControl_WaitUltrasonicEchoState(TRUE,
                                            BODY_ULTRASONIC_ECHO_TIMEOUT_US) == FALSE)
    {
        return FALSE;
    }

    pulseStartTicks = BodyControl_GetStmTicks();
    timeoutTicks = BodyControl_GetTicksFromUs(BODY_ULTRASONIC_ECHO_TIMEOUT_US);

    while (BodyControl_ReadUltrasonicEchoHigh() != FALSE)
    {
        if ((BodyControl_GetStmTicks() - pulseStartTicks) >= timeoutTicks)
        {
            return FALSE;
        }
    }

    pulseTicks = BodyControl_GetStmTicks() - pulseStartTicks;
    ticksPerUs = BodyControl_GetTicksFromUs(1U);
    pulseUs = pulseTicks / ticksPerUs;
    distance = pulseUs / 58U;

    if (distance > BODY_ULTRASONIC_NO_OBJECT_CM)
    {
        distance = BODY_ULTRASONIC_NO_OBJECT_CM;
    }

    *distanceCm = (uint16)distance;
    return TRUE;
}

static uint8 BodyControl_ComputeCollisionWarningLevel(uint16 distanceCm,
                                                      boolean distanceValid)
{
    uint16 enterStage1Cm;
    uint16 enterStage2Cm;
    uint16 enterStage3Cm;
    uint16 exitStage0Cm;
    uint16 exitStage1Cm;
    uint16 exitStage2Cm;

    if (distanceValid == FALSE)
    {
        return 0U;
    }

    enterStage1Cm = BODY_COLLISION_WARNING_STAGE1_CM - BODY_COLLISION_WARNING_HYSTERESIS_CM;
    enterStage2Cm = BODY_COLLISION_WARNING_STAGE2_CM - BODY_COLLISION_WARNING_HYSTERESIS_CM;
    enterStage3Cm = BODY_COLLISION_WARNING_STAGE3_CM - BODY_COLLISION_WARNING_HYSTERESIS_CM;

    exitStage0Cm = BODY_COLLISION_WARNING_STAGE1_CM + BODY_COLLISION_WARNING_HYSTERESIS_CM;
    exitStage1Cm = BODY_COLLISION_WARNING_STAGE2_CM + BODY_COLLISION_WARNING_HYSTERESIS_CM;
    exitStage2Cm = BODY_COLLISION_WARNING_STAGE3_CM + BODY_COLLISION_WARNING_HYSTERESIS_CM;

    switch (g_collisionWarningLevel)
    {
        case 0U:
            if (distanceCm <= enterStage3Cm)
            {
                return 3U;
            }

            if (distanceCm <= enterStage2Cm)
            {
                return 2U;
            }

            if (distanceCm <= enterStage1Cm)
            {
                return 1U;
            }

            return 0U;

        case 1U:
            if (distanceCm <= enterStage3Cm)
            {
                return 3U;
            }

            if (distanceCm <= enterStage2Cm)
            {
                return 2U;
            }

            if (distanceCm >= exitStage0Cm)
            {
                return 0U;
            }

            return 1U;

        case 2U:
            if (distanceCm <= enterStage3Cm)
            {
                return 3U;
            }

            if (distanceCm >= exitStage0Cm)
            {
                return 0U;
            }

            if (distanceCm >= exitStage1Cm)
            {
                return 1U;
            }

            return 2U;

        case 3U:
            if (distanceCm >= exitStage0Cm)
            {
                return 0U;
            }

            if (distanceCm >= exitStage1Cm)
            {
                return 1U;
            }

            if (distanceCm >= exitStage2Cm)
            {
                return 2U;
            }

            return 3U;

        default:
            return 0U;
    }
}

static void BodyControl_ResetCollisionWarningSound(void)
{
    g_collisionWarningLevel = 0U;
    g_collisionWarningLastLevel = 0U;
    g_collisionWarningToneOutputOn = FALSE;
    g_collisionWarningToneTimerMs = 0U;
    g_collisionWarningPatternTimerMs = 0U;
}

static void BodyControl_UpdateUltrasonic1ms(void)
{
    uint16 measuredDistanceCm;

    if (g_collisionWarningLampOn == FALSE)
    {
        g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
        g_ultrasonicDistanceValid = FALSE;
        g_ultrasonicMeasureTimerMs = 0U;
        g_ultrasonicValidHoldTimerMs = 0U;
        BodyControl_ResetCollisionWarningSound();
        return;
    }

    if (g_ultrasonicValidHoldTimerMs > 0U)
    {
        g_ultrasonicValidHoldTimerMs--;
    }

    if (g_ultrasonicMeasureTimerMs < BODY_ULTRASONIC_MEASURE_PERIOD_MS)
    {
        g_ultrasonicMeasureTimerMs++;
        return;
    }

    g_ultrasonicMeasureTimerMs = 0U;
    g_ultrasonicMeasureCount++;

    if (BodyControl_MeasureUltrasonicDistanceCm(&measuredDistanceCm) != FALSE)
    {
        g_ultrasonicDistanceCm = measuredDistanceCm;
        g_ultrasonicDistanceValid = TRUE;
        g_ultrasonicValidHoldTimerMs = BODY_ULTRASONIC_VALID_HOLD_MS;
    }
    else
    {
        g_ultrasonicTimeoutCount++;
        g_diagFaultMask |= BODY_DIAG_ULTRASONIC_BIT;
        g_debugBodyDiagFaultMask = g_diagFaultMask;
        BodyControl_UpdateDiagDangerLed();

        if (g_ultrasonicValidHoldTimerMs == 0U)
        {
            g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
            g_ultrasonicDistanceValid = FALSE;
        }
    }

    g_collisionWarningLevel =
        BodyControl_ComputeCollisionWarningLevel(g_ultrasonicDistanceCm,
                                                 g_ultrasonicDistanceValid);
}

static boolean BodyControl_IsCollisionWarningBeepActive(void)
{
    uint32 cycleMs;

    if (g_collisionWarningLevel == 0U)
    {
        return FALSE;
    }

    if (g_collisionWarningLevel == 3U)
    {
        return TRUE;
    }

    cycleMs = (g_collisionWarningLevel == 2U) ?
              BODY_COLLISION_WARNING_STAGE2_CYCLE_MS :
              BODY_COLLISION_WARNING_STAGE1_CYCLE_MS;

    if (g_collisionWarningPatternTimerMs >= cycleMs)
    {
        g_collisionWarningPatternTimerMs = 0U;
    }

    return (g_collisionWarningPatternTimerMs < BODY_COLLISION_WARNING_BEEP_ON_MS) ? TRUE : FALSE;
}

static void BodyControl_UpdateCollisionWarningSound1ms(void)
{
    boolean beepActive;

    if ((g_collisionWarningLampOn == FALSE) ||
        (g_collisionWarningLevel == 0U))
    {
        BodyControl_ResetCollisionWarningSound();

        if (g_hornEnabled == FALSE)
        {
            BodyControl_BuzzerOff();
        }

        return;
    }

    if (g_collisionWarningLevel != g_collisionWarningLastLevel)
    {
        g_collisionWarningLastLevel = g_collisionWarningLevel;
        g_collisionWarningToneOutputOn = FALSE;
        g_collisionWarningToneTimerMs = 0U;
        g_collisionWarningPatternTimerMs = 0U;
    }

    if (g_hornEnabled != FALSE)
    {
        return;
    }

    beepActive = BodyControl_IsCollisionWarningBeepActive();

    if (beepActive == FALSE)
    {
        g_collisionWarningToneOutputOn = FALSE;
        g_collisionWarningToneTimerMs = 0U;
        BodyControl_BuzzerOff();
        g_collisionWarningPatternTimerMs++;
        return;
    }

    g_collisionWarningToneTimerMs++;

    if (g_collisionWarningToneTimerMs >= BODY_COLLISION_WARNING_TONE_HALF_MS)
    {
        g_collisionWarningToneTimerMs = 0U;

        if (g_collisionWarningToneOutputOn == FALSE)
        {
            g_collisionWarningToneOutputOn = TRUE;
        }
        else
        {
            g_collisionWarningToneOutputOn = FALSE;
        }

        BodyControl_WriteBuzzerPin(g_collisionWarningToneOutputOn);
    }

    if (g_collisionWarningLevel != 3U)
    {
        g_collisionWarningPatternTimerMs++;
    }
}

static void BodyControl_UpdateCollisionWarningDebug(void)
{
    g_debugBodyUltrasonicDistanceCm = g_ultrasonicDistanceCm;
    g_debugBodyUltrasonicDistanceValid = (g_ultrasonicDistanceValid != FALSE) ? 1U : 0U;
    g_debugBodyCollisionWarningLevel = g_collisionWarningLevel;
    g_debugBodyUltrasonicMeasureCount = g_ultrasonicMeasureCount;
    g_debugBodyUltrasonicTimeoutCount = g_ultrasonicTimeoutCount;
}

static uint8 BodyControl_ReadDiagFeedbackMaskRaw(void)
{
    uint8 mask;

    mask = 0U;

    if (BodyControl_ReadDiagPin(BODY_DIAG_BRAKE_FB_PORT,
                                BODY_DIAG_BRAKE_FB_PIN) != FALSE)
    {
        mask |= BODY_DIAG_BRAKE_LAMP_BIT;
    }

    if (BodyControl_ReadDiagPin(BODY_DIAG_HEAD_FB_PORT,
                                BODY_DIAG_HEAD_FB_PIN) != FALSE)
    {
        mask |= BODY_DIAG_HEADLAMP_BIT;
    }

    if (BodyControl_ReadDiagPin(BODY_DIAG_LEFT_FB_PORT,
                                BODY_DIAG_LEFT_FB_PIN) != FALSE)
    {
        mask |= BODY_DIAG_LEFT_TURN_BIT;
    }

    if (BodyControl_ReadDiagPin(BODY_DIAG_RIGHT_FB_PORT,
                                BODY_DIAG_RIGHT_FB_PIN) != FALSE)
    {
        mask |= BODY_DIAG_RIGHT_TURN_BIT;
    }

    return mask;
}

static void BodyControl_WriteDiagDangerLed(boolean on)
{
    BodyControl_WriteNormalLedPin(BODY_DIAG_DANGER_LED_PORT,
                                  BODY_DIAG_DANGER_LED_PIN,
                                  on);
}

static void BodyControl_UpdateDiagDangerLed(void)
{
    BodyControl_WriteDiagDangerLed((g_diagFaultMask != 0U) ? TRUE : FALSE);
}

static void BodyControl_RunStartupSelfDiagnosis(void)
{
    uint8 feedbackMask;
    uint8 startupFaultMask;

    BodyControl_WriteHeadlamp(TRUE);
    BodyControl_WriteBrakePin(TRUE);

    /* P02.6: left turn signal LED */
    BodyControl_WriteNormalLedPin(BODY_LEFT_TURN_PORT,
                                  BODY_LEFT_TURN_PIN,
                                  TRUE);

    /* P02.4: right turn signal LED */
    BodyControl_WriteNormalLedPin(BODY_RIGHT_TURN_PORT,
                                  BODY_RIGHT_TURN_PIN,
                                  TRUE);

    BodyControl_DelayMs(BODY_DIAG_STARTUP_SELF_TEST_MS);

    feedbackMask = BodyControl_ReadDiagFeedbackMaskRaw();
    startupFaultMask = 0U;

    /* P00.0: brake lamp diagnosis feedback */
    if ((feedbackMask & BODY_DIAG_BRAKE_LAMP_BIT) == 0U)
    {
        startupFaultMask |= BODY_DIAG_BRAKE_LAMP_BIT;
    }

    /* P00.2: headlamp diagnosis feedback */
    if ((feedbackMask & BODY_DIAG_HEADLAMP_BIT) == 0U)
    {
        startupFaultMask |= BODY_DIAG_HEADLAMP_BIT;
    }

    /* P00.6: left turn signal diagnosis feedback */
    if ((feedbackMask & BODY_DIAG_LEFT_TURN_BIT) == 0U)
    {
        startupFaultMask |= BODY_DIAG_LEFT_TURN_BIT;
    }

    /* P00.8: right turn signal diagnosis feedback */
    if ((feedbackMask & BODY_DIAG_RIGHT_TURN_BIT) == 0U)
    {
        startupFaultMask |= BODY_DIAG_RIGHT_TURN_BIT;
    }

    g_diagFaultMask |= startupFaultMask;
    g_diagFeedbackMask = feedbackMask;
    g_debugBodyDiagStartupFaultMask = startupFaultMask;
    g_debugBodyDiagFaultMask = g_diagFaultMask;
    g_debugBodyDiagFeedbackMask = g_diagFeedbackMask;

    BodyControl_WriteHeadlamp(FALSE);
    BodyControl_WriteBrakePin(FALSE);

    BodyControl_WriteNormalLedPin(BODY_LEFT_TURN_PORT,
                                  BODY_LEFT_TURN_PIN,
                                  FALSE);

    BodyControl_WriteNormalLedPin(BODY_RIGHT_TURN_PORT,
                                  BODY_RIGHT_TURN_PIN,
                                  FALSE);

    BodyControl_UpdateDiagDangerLed();
}

static void BodyControl_UpdateDiagOneSignal(boolean commandOn,
                                            boolean feedbackOn,
                                            uint32 confirmMs,
                                            uint32* lowTimerMs,
                                            uint8 faultBit)
{
    if (commandOn == FALSE)
    {
        *lowTimerMs = 0U;
        return;
    }

    if (feedbackOn != FALSE)
    {
        *lowTimerMs = 0U;
        return;
    }

    if (*lowTimerMs < confirmMs)
    {
        (*lowTimerMs)++;
    }

    if (*lowTimerMs >= confirmMs)
    {
        g_diagFaultMask |= faultBit;
    }
}

static void BodyControl_UpdateDiagnosis1ms(void)
{
    boolean brakeCommandOn;
    boolean headCommandOn;
    boolean leftCommandOn;
    boolean rightCommandOn;

    boolean brakeFeedbackOn;
    boolean headFeedbackOn;
    boolean leftFeedbackOn;
    boolean rightFeedbackOn;

    g_diagCommandMask = 0U;

    brakeCommandOn = g_brakeLampOn;
    headCommandOn = g_headlampOn;

    /*
     * 방향지시등은 점멸 출력이다.
     * 여기서는 "좌/우 램프가 기능적으로 요구되는 상태"를 command로 본다.
     * collision warning lamp ON이면 비상등처럼 좌/우가 모두 요구된다.
     */
    leftCommandOn = (g_leftTurnEnabled != FALSE) ? TRUE : FALSE;
    rightCommandOn = (g_rightTurnEnabled != FALSE) ? TRUE : FALSE;

    if (brakeCommandOn != FALSE)
    {
        g_diagCommandMask |= BODY_DIAG_BRAKE_LAMP_BIT;
    }

    if (headCommandOn != FALSE)
    {
        g_diagCommandMask |= BODY_DIAG_HEADLAMP_BIT;
    }

    if (leftCommandOn != FALSE)
    {
        g_diagCommandMask |= BODY_DIAG_LEFT_TURN_BIT;
    }

    if (rightCommandOn != FALSE)
    {
        g_diagCommandMask |= BODY_DIAG_RIGHT_TURN_BIT;
    }

    if (g_collisionWarningLampOn != FALSE)
    {
        g_diagCommandMask |= BODY_DIAG_ULTRASONIC_BIT;
    }

    g_diagFeedbackMask = BodyControl_ReadDiagFeedbackMaskRaw();

    brakeFeedbackOn = ((g_diagFeedbackMask & BODY_DIAG_BRAKE_LAMP_BIT) != 0U) ? TRUE : FALSE;
    headFeedbackOn = ((g_diagFeedbackMask & BODY_DIAG_HEADLAMP_BIT) != 0U) ? TRUE : FALSE;
    leftFeedbackOn = ((g_diagFeedbackMask & BODY_DIAG_LEFT_TURN_BIT) != 0U) ? TRUE : FALSE;
    rightFeedbackOn = ((g_diagFeedbackMask & BODY_DIAG_RIGHT_TURN_BIT) != 0U) ? TRUE : FALSE;

    BodyControl_UpdateDiagOneSignal(brakeCommandOn,
                                    brakeFeedbackOn,
                                    BODY_DIAG_STEADY_CONFIRM_MS,
                                    &g_diagBrakeLowTimerMs,
                                    BODY_DIAG_BRAKE_LAMP_BIT);

    BodyControl_UpdateDiagOneSignal(headCommandOn,
                                    headFeedbackOn,
                                    BODY_DIAG_STEADY_CONFIRM_MS,
                                    &g_diagHeadLowTimerMs,
                                    BODY_DIAG_HEADLAMP_BIT);

    BodyControl_UpdateDiagOneSignal(leftCommandOn,
                                    leftFeedbackOn,
                                    BODY_DIAG_BLINK_CONFIRM_MS,
                                    &g_diagLeftLowTimerMs,
                                    BODY_DIAG_LEFT_TURN_BIT);

    BodyControl_UpdateDiagOneSignal(rightCommandOn,
                                    rightFeedbackOn,
                                    BODY_DIAG_BLINK_CONFIRM_MS,
                                    &g_diagRightLowTimerMs,
                                    BODY_DIAG_RIGHT_TURN_BIT);

    g_debugBodyDiagFaultMask = g_diagFaultMask;
    g_debugBodyDiagCommandMask = g_diagCommandMask;
    g_debugBodyDiagFeedbackMask = g_diagFeedbackMask;
    BodyControl_UpdateDiagDangerLed();
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

static boolean BodyControl_IsBlinkRequested(boolean leftOn,
                                            boolean rightOn)
{
    return ((leftOn != FALSE) ||
            (rightOn != FALSE)) ? TRUE : FALSE;
}

static void BodyControl_SetBlinkRequest(boolean leftOn,
                                        boolean rightOn)
{
    boolean oldRequested;
    boolean newRequested;
    boolean requestChanged;

    oldRequested = BodyControl_IsBlinkRequested(g_leftTurnEnabled,
                                                g_rightTurnEnabled);

    requestChanged = ((g_leftTurnEnabled != leftOn) ||
                      (g_rightTurnEnabled != rightOn)) ? TRUE : FALSE;

    g_leftTurnEnabled = leftOn;
    g_rightTurnEnabled = rightOn;

    newRequested = BodyControl_IsBlinkRequested(g_leftTurnEnabled,
                                                g_rightTurnEnabled);

    if (newRequested == FALSE)
    {
        g_blinkOutputOn = FALSE;
        g_blinkTimerMs = 0U;
    }
    else if ((oldRequested == FALSE) || (requestChanged != FALSE))
    {
        g_blinkOutputOn = TRUE;
        g_blinkTimerMs = 0U;
    }

    BodyControl_ApplyOutputs();
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

static void BodyControl_UpdateHorn1ms(void)
{
    if (g_hornEnabled == FALSE)
    {
        g_hornOutputOn = FALSE;
        g_hornTimerMs = 0U;
        BodyControl_WriteBuzzerPin(FALSE);
        return;
    }

    /*
     * 1ms마다 토글 = 약 500Hz 경적음.
     * passive buzzer 기준으로 CAN 명령을 block 없이 유지하기 위한 간단한 방식.
     */
    g_hornTimerMs++;

    if (g_hornTimerMs >= 1U)
    {
        g_hornTimerMs = 0U;

        if (g_hornOutputOn == FALSE)
        {
            g_hornOutputOn = TRUE;
        }
        else
        {
            g_hornOutputOn = FALSE;
        }

        BodyControl_WriteBuzzerPin(g_hornOutputOn);
    }
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
    if (distanceCm >= BODY_COLLISION_WARNING_STAGE1_CM)
    {
        BodyControl_SilentForMs(holdTimeMs);
        return;
    }

    /*
     * 3단계: 5cm 미만
     * 연속 삐---
     */
    if (distanceCm <= BODY_COLLISION_WARNING_STAGE3_CM)
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
    if (distanceCm <= BODY_COLLISION_WARNING_STAGE2_CM)
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

    if (g_collisionWarningLampOn != FALSE)
    {
        /*
         * 충돌 방지 경고등 ON:
         * 별도 경고등 핀이 없으므로 비상등처럼 좌/우 동시 점멸한다.
         */
        leftLedOn = (g_blinkOutputOn != FALSE) ? TRUE : FALSE;
        rightLedOn = (g_blinkOutputOn != FALSE) ? TRUE : FALSE;
    }
    else
    {
        leftLedOn = ((g_leftTurnEnabled != FALSE) &&
                     (g_blinkOutputOn != FALSE)) ? TRUE : FALSE;

        rightLedOn = ((g_rightTurnEnabled != FALSE) &&
                      (g_blinkOutputOn != FALSE)) ? TRUE : FALSE;
    }

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

    /*
     * Ultrasonic sensor
     */
    BodyControl_InitInputPinPulldown(BODY_ULTRASONIC_ECHO_PORT,
                                     BODY_ULTRASONIC_ECHO_PIN);

    BodyControl_InitOutputPin(BODY_ULTRASONIC_TRIG_PORT,
                              BODY_ULTRASONIC_TRIG_PIN);
    BodyControl_WriteUltrasonicTrig(FALSE);

    /*
     * Collision event button
     */
#if BODY_COLLISION_BUTTON_ACTIVE_LOW
    BodyControl_InitInputPinPullup(BODY_COLLISION_BUTTON_PORT,
                                   BODY_COLLISION_BUTTON_PIN);
#else
    BodyControl_InitInputPinPulldown(BODY_COLLISION_BUTTON_PORT,
                                     BODY_COLLISION_BUTTON_PIN);
#endif

    /*
     * Lamp diagnosis feedback inputs
     */
    BodyControl_InitInputPinPulldown(BODY_DIAG_BRAKE_FB_PORT,
                                     BODY_DIAG_BRAKE_FB_PIN);

    BodyControl_InitInputPinPulldown(BODY_DIAG_HEAD_FB_PORT,
                                     BODY_DIAG_HEAD_FB_PIN);

    BodyControl_InitInputPinPulldown(BODY_DIAG_LEFT_FB_PORT,
                                     BODY_DIAG_LEFT_FB_PIN);

    BodyControl_InitInputPinPulldown(BODY_DIAG_RIGHT_FB_PORT,
                                     BODY_DIAG_RIGHT_FB_PIN);

    /*
     * Diagnosis danger indicator
     */
    BodyControl_InitOutputPin(BODY_DIAG_DANGER_LED_PORT,
                              BODY_DIAG_DANGER_LED_PIN);

    g_headlampOn = FALSE;
    g_brakeLampOn = FALSE;

    g_leftTurnEnabled = FALSE;
    g_rightTurnEnabled = FALSE;
    g_collisionWarningLampOn = FALSE;

    g_hornEnabled = FALSE;
    g_hornOutputOn = FALSE;
    g_hornTimerMs = 0U;

    g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
    g_ultrasonicDistanceValid = FALSE;
    g_ultrasonicMeasureTimerMs = 0U;
    g_ultrasonicValidHoldTimerMs = 0U;
    g_ultrasonicMeasureCount = 0U;
    g_ultrasonicTimeoutCount = 0U;

    BodyControl_ResetCollisionWarningSound();

    g_blinkOutputOn = FALSE;
    g_blinkTimerMs = 0U;

    g_collisionButtonLastRawPressed = BodyControl_ReadCollisionButtonRawPressed();
    g_collisionButtonStablePressed = g_collisionButtonLastRawPressed;
    g_collisionButtonEventPending = FALSE;
    g_collisionButtonDebounceTimerMs = 0U;
    g_collisionButtonPressedEventCount = 0U;

    g_diagFaultMask = 0U;
    g_diagCommandMask = 0U;
    g_diagFeedbackMask = 0U;

    g_diagBrakeLowTimerMs = 0U;
    g_diagHeadLowTimerMs = 0U;
    g_diagLeftLowTimerMs = 0U;
    g_diagRightLowTimerMs = 0U;

    g_debugBodyDiagFaultMask = 0U;
    g_debugBodyDiagCommandMask = 0U;
    g_debugBodyDiagFeedbackMask = 0U;
    g_debugBodyDiagStartupFaultMask = 0U;

    g_debugBodyCollisionButtonRawPressed =
        (g_collisionButtonLastRawPressed != FALSE) ? 1U : 0U;
    g_debugBodyCollisionButtonStablePressed =
        (g_collisionButtonStablePressed != FALSE) ? 1U : 0U;
    g_debugBodyCollisionButtonEventCount = 0U;

    g_debugBodyUltrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
    g_debugBodyUltrasonicDistanceValid = 0U;
    g_debugBodyCollisionWarningLevel = 0U;
    g_debugBodyUltrasonicMeasureCount = 0U;
    g_debugBodyUltrasonicTimeoutCount = 0U;

    BodyControl_RunStartupSelfDiagnosis();

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
    BodyControl_SetBlinkRequest(on,
                                g_rightTurnEnabled);
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
    BodyControl_SetBlinkRequest(g_leftTurnEnabled,
                                on);
}

void BodyControl_RightTurnOn(void)
{
    BodyControl_SetRightTurn(TRUE);
}

void BodyControl_RightTurnOff(void)
{
    BodyControl_SetRightTurn(FALSE);
}

void BodyControl_SetTurnSignal(uint8 turnMode)
{
    boolean leftOn;
    boolean rightOn;

    leftOn = FALSE;
    rightOn = FALSE;

    switch (turnMode)
    {
        case BODY_CONTROL_TURN_LEFT:
            leftOn = TRUE;
            break;

        case BODY_CONTROL_TURN_RIGHT:
            rightOn = TRUE;
            break;

        case BODY_CONTROL_TURN_HAZARD:
            leftOn = TRUE;
            rightOn = TRUE;
            break;

        case BODY_CONTROL_TURN_OFF:
        default:
            break;
    }

    BodyControl_SetBlinkRequest(leftOn,
                                rightOn);
}

void BodyControl_SetCollisionWarningLamp(boolean on)
{
    boolean wasOn;

    wasOn = g_collisionWarningLampOn;

    g_collisionWarningLampOn = on;

    if (on != FALSE)
    {
        if (wasOn == FALSE)
        {
            g_ultrasonicMeasureTimerMs = BODY_ULTRASONIC_MEASURE_PERIOD_MS;
        }
    }
    else
    {
        g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
        g_ultrasonicDistanceValid = FALSE;
        g_ultrasonicMeasureTimerMs = 0U;
        g_ultrasonicValidHoldTimerMs = 0U;
        BodyControl_ResetCollisionWarningSound();

        if (g_hornEnabled == FALSE)
        {
            BodyControl_BuzzerOff();
        }
    }
}

void BodyControl_SetHorn(boolean on)
{
    g_hornEnabled = on;

    if (on == FALSE)
    {
        g_hornOutputOn = FALSE;
        g_hornTimerMs = 0U;
        BodyControl_BuzzerOff();
    }
}

boolean BodyControl_IsCollisionButtonPressed(void)
{
    return g_collisionButtonStablePressed;
}

boolean BodyControl_ConsumeCollisionButtonPressedEvent(void)
{
    if (g_collisionButtonEventPending == FALSE)
    {
        return FALSE;
    }

    g_collisionButtonEventPending = FALSE;
    return TRUE;
}

/* All Off */

void BodyControl_AllOff(void)
{
    g_headlampOn = FALSE;
    g_brakeLampOn = FALSE;

    g_leftTurnEnabled = FALSE;
    g_rightTurnEnabled = FALSE;
    g_collisionWarningLampOn = FALSE;

    g_hornEnabled = FALSE;
    g_hornOutputOn = FALSE;
    g_hornTimerMs = 0U;

    g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
    g_ultrasonicDistanceValid = FALSE;
    g_ultrasonicMeasureTimerMs = 0U;
    g_ultrasonicValidHoldTimerMs = 0U;
    BodyControl_ResetCollisionWarningSound();

    g_blinkOutputOn = FALSE;
    g_blinkTimerMs = 0U;

    g_diagCommandMask = 0U;
    g_diagFeedbackMask = BodyControl_ReadDiagFeedbackMaskRaw();

    g_diagBrakeLowTimerMs = 0U;
    g_diagHeadLowTimerMs = 0U;
    g_diagLeftLowTimerMs = 0U;
    g_diagRightLowTimerMs = 0U;

    g_debugBodyDiagCommandMask = g_diagCommandMask;
    g_debugBodyDiagFeedbackMask = g_diagFeedbackMask;
    BodyControl_UpdateDiagDangerLed();
    BodyControl_UpdateCollisionWarningDebug();

    BodyControl_BuzzerOff();
    BodyControl_ApplyOutputs();
}

/* Diagnosis public functions */

void BodyControl_ClearDiagFaults(void)
{
    g_diagFaultMask = 0U;

    g_diagBrakeLowTimerMs = 0U;
    g_diagHeadLowTimerMs = 0U;
    g_diagLeftLowTimerMs = 0U;
    g_diagRightLowTimerMs = 0U;

    g_debugBodyDiagFaultMask = g_diagFaultMask;
    BodyControl_UpdateDiagDangerLed();
}

uint8 BodyControl_GetDiagFaultMask(void)
{
    return g_diagFaultMask;
}

uint8 BodyControl_GetDiagCommandMask(void)
{
    return g_diagCommandMask;
}

uint8 BodyControl_GetDiagFeedbackMask(void)
{
    return g_diagFeedbackMask;
}

uint16 BodyControl_GetUltrasonicDistanceCm(void)
{
    return g_ultrasonicDistanceCm;
}

boolean BodyControl_IsUltrasonicDistanceValid(void)
{
    return g_ultrasonicDistanceValid;
}

uint8 BodyControl_GetCollisionWarningLevel(void)
{
    return g_collisionWarningLevel;
}

void BodyControl_RunBuzzerDiagnosticRoutine(void)
{
    BodyControl_HornForMs(500U);
}

void BodyControl_RunLedDiagnosticRoutine(void)
{
    BodyControl_WriteBrakePin(TRUE);
    BodyControl_WriteNormalLedPin(BODY_LEFT_TURN_PORT,
                                  BODY_LEFT_TURN_PIN,
                                  TRUE);
    BodyControl_WriteNormalLedPin(BODY_RIGHT_TURN_PORT,
                                  BODY_RIGHT_TURN_PIN,
                                  TRUE);

    BodyControl_DelayMs(1000U);

    BodyControl_WriteBrakePin(FALSE);
    BodyControl_WriteNormalLedPin(BODY_LEFT_TURN_PORT,
                                  BODY_LEFT_TURN_PIN,
                                  FALSE);
    BodyControl_WriteNormalLedPin(BODY_RIGHT_TURN_PORT,
                                  BODY_RIGHT_TURN_PIN,
                                  FALSE);

    BodyControl_ApplyOutputs();
}

boolean BodyControl_RunUltrasonicDiagnosticRoutine(uint16* distanceMm)
{
    uint16 distanceCm;
    uint32 distance;

    if (distanceMm == NULL_PTR)
    {
        return FALSE;
    }

    if (BodyControl_MeasureUltrasonicDistanceCm(&distanceCm) == FALSE)
    {
        g_diagFaultMask |= BODY_DIAG_ULTRASONIC_BIT;
        g_ultrasonicDistanceCm = BODY_ULTRASONIC_NO_OBJECT_CM;
        g_ultrasonicDistanceValid = FALSE;
        g_ultrasonicTimeoutCount++;
        g_debugBodyDiagFaultMask = g_diagFaultMask;
        g_debugBodyUltrasonicDistanceCm = g_ultrasonicDistanceCm;
        g_debugBodyUltrasonicDistanceValid = 0U;
        g_debugBodyUltrasonicTimeoutCount = g_ultrasonicTimeoutCount;
        BodyControl_UpdateDiagDangerLed();
        *distanceMm = 0xFFFFU;
        return FALSE;
    }

    distance = ((uint32)distanceCm) * 10U;

    if (distance > 0xFFFFU)
    {
        distance = 0xFFFFU;
    }

    g_ultrasonicDistanceCm = distanceCm;
    g_ultrasonicDistanceValid = TRUE;
    g_ultrasonicMeasureCount++;
    g_debugBodyUltrasonicDistanceCm = g_ultrasonicDistanceCm;
    g_debugBodyUltrasonicDistanceValid = 1U;
    g_debugBodyUltrasonicMeasureCount = g_ultrasonicMeasureCount;

    *distanceMm = (uint16)distance;
    return TRUE;
}

/* Update */

void BodyControl_Update1ms(void)
{
    BodyControl_UpdateCollisionButton1ms();
    BodyControl_UpdateUltrasonic1ms();
    BodyControl_UpdateHorn1ms();
    BodyControl_UpdateCollisionWarningSound1ms();

    if ((g_leftTurnEnabled == FALSE) &&
        (g_rightTurnEnabled == FALSE))
    {
        g_blinkOutputOn = FALSE;
        g_blinkTimerMs = 0U;
        BodyControl_ApplyOutputs();
        BodyControl_UpdateDiagnosis1ms();
        BodyControl_UpdateCollisionWarningDebug();
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

    BodyControl_UpdateDiagnosis1ms();
    BodyControl_UpdateCollisionWarningDebug();
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
