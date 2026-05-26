#include "Servo.h"

#include "Ifx_Types.h"
#include "Bsp.h"
#include "IfxPort.h"
#include "IfxStm.h"

#include "Motor.h"

/*
 * Servo Signal Pin
 * P02.3 = X304 6번 핀
 */
#define SERVO_PORT        (&MODULE_P02)
#define SERVO_PIN         (3U)

/*
 * Standard RC Servo PWM
 * Period: 20ms = 20000us
 */
#define SERVO_PERIOD_US   (20000U)

/*
 * 서보에 실제로 허용할 절대 펄스 범위.
 */
#define SERVO_MIN_US      (950U)
#define SERVO_MAX_US      (1400U)

/*
 * 조향 최대각/중앙값.
 */
#define STEERING_LEFT_US      (1400U)
#define STEERING_MIDDLE_US    (1200U)
#define STEERING_RIGHT_US     (950U)

/*
 * Steering command mode.
 * Steering command changes the target pulse immediately.
 */
static SteeringState g_steeringState = STEERING_MIDDLE;
static SteeringKey g_steeringKey = STEERING_KEY_NULL;
static uint32 g_currentPulseUs = STEERING_MIDDLE_US;

static void delayUs(uint32 us)
{
    waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, us));
}

static void delayLowUsWithMotorUpdate(uint32 us)
{
    while (us >= 1000U)
    {
        MotorControl_Update();
        us -= 1000U;
    }

    if (us > 0U)
    {
        delayUs(us);
    }
}

static uint32 Servo_LimitPulseUs(uint32 pulseUs)
{
    if (pulseUs < SERVO_MIN_US)
    {
        pulseUs = SERVO_MIN_US;
    }

    if (pulseUs > SERVO_MAX_US)
    {
        pulseUs = SERVO_MAX_US;
    }

    return pulseUs;
}

static uint32 Servo_AbsDiffUs(uint32 a, uint32 b)
{
    return (a > b) ? (a - b) : (b - a);
}

static uint32 Steering_StateToPulseUs(SteeringState state)
{
    uint32 pulseUs;

    switch (state)
    {
        case STEERING_LEFT:
            pulseUs = STEERING_LEFT_US;
            break;

        case STEERING_RIGHT:
            pulseUs = STEERING_RIGHT_US;
            break;

        case STEERING_MIDDLE:
        default:
            pulseUs = STEERING_MIDDLE_US;
            break;
    }

    return Servo_LimitPulseUs(pulseUs);
}

static void Steering_UpdateStateFromPulse(void)
{
    uint32 leftDiff;
    uint32 middleDiff;
    uint32 rightDiff;

    leftDiff = Servo_AbsDiffUs(g_currentPulseUs, STEERING_LEFT_US);
    middleDiff = Servo_AbsDiffUs(g_currentPulseUs, STEERING_MIDDLE_US);
    rightDiff = Servo_AbsDiffUs(g_currentPulseUs, STEERING_RIGHT_US);

    if ((leftDiff <= middleDiff) && (leftDiff <= rightDiff))
    {
        g_steeringState = STEERING_LEFT;
    }
    else if ((rightDiff <= middleDiff) && (rightDiff <= leftDiff))
    {
        g_steeringState = STEERING_RIGHT;
    }
    else
    {
        g_steeringState = STEERING_MIDDLE;
    }
}

static void Steering_SetTargetPulseImmediate(uint32 targetPulseUs)
{
    g_currentPulseUs = Servo_LimitPulseUs(targetPulseUs);
    /* Jump to the requested pulse immediately. */
    Steering_UpdateStateFromPulse();
}

static void Steering_UpdatePulseByKey(void)
{
    switch (g_steeringKey)
    {
        case STEERING_KEY_LEFT:
            /* 왼쪽 신호 유지: 왼쪽 최대각까지 연속 이동, 이후에는 더 이상 이동하지 않음 */
            Steering_SetTargetPulseImmediate(STEERING_LEFT_US);
            break;

        case STEERING_KEY_RIGHT:
            /* 오른쪽 신호 유지: 오른쪽 최대각까지 연속 이동, 이후에는 더 이상 이동하지 않음 */
            Steering_SetTargetPulseImmediate(STEERING_RIGHT_US);
            break;

        case STEERING_KEY_NULL:
        default:
            /* No steering command: return to center immediately. */
            Steering_SetTargetPulseImmediate(STEERING_MIDDLE_US);
            break;
    }
}

void Steering_Init(void)
{
    IfxPort_setPinModeOutput(SERVO_PORT,
                             SERVO_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinLow(SERVO_PORT, SERVO_PIN);

    g_steeringState = STEERING_MIDDLE;
    g_steeringKey = STEERING_KEY_NULL;
    g_currentPulseUs = Servo_LimitPulseUs(STEERING_MIDDLE_US);
}

void Steering_SetState(SteeringState state)
{
    if ((state == STEERING_LEFT) ||
        (state == STEERING_MIDDLE) ||
        (state == STEERING_RIGHT))
    {
        g_steeringKey = STEERING_KEY_NULL;
        g_steeringState = state;
        g_currentPulseUs = Steering_StateToPulseUs(state);
    }
}

SteeringState Steering_GetState(void)
{
    return g_steeringState;
}

void Steering_SetKey(SteeringKey key)
{
    if ((key == STEERING_KEY_NULL) ||
        (key == STEERING_KEY_LEFT) ||
        (key == STEERING_KEY_RIGHT))
    {
        g_steeringKey = key;

        switch (key)
        {
            case STEERING_KEY_LEFT:
                Steering_SetTargetPulseImmediate(STEERING_LEFT_US);
                break;

            case STEERING_KEY_RIGHT:
                Steering_SetTargetPulseImmediate(STEERING_RIGHT_US);
                break;

            case STEERING_KEY_NULL:
            default:
                Steering_SetTargetPulseImmediate(STEERING_MIDDLE_US);
                break;
        }
    }
}

SteeringKey Steering_GetKey(void)
{
    return g_steeringKey;
}

void Steering_Center(void)
{
    Steering_SetState(STEERING_MIDDLE);
}

void Steering_SetPulseUs(uint32 pulseUs)
{
    g_steeringKey = STEERING_KEY_NULL;
    g_currentPulseUs = Servo_LimitPulseUs(pulseUs);
    Steering_UpdateStateFromPulse();
}

uint32 Steering_GetPulseUs(void)
{
    return g_currentPulseUs;
}

void Steering_Update(void)
{
    /* Apply the current key state immediately. */
    Steering_UpdatePulseByKey();

    /* 이동된 현재 pulse로 서보 펄스 1회 출력 */
    Servo_SendOnePulse(g_currentPulseUs);
}

void Steering_UpdateForMs(uint32 holdTimeMs)
{
    uint32 count;
    uint32 i;

    /* Steering_Update() 1회 = 약 20ms */
    count = holdTimeMs / 20U;

    if (count == 0U)
    {
        count = 1U;
    }

    for (i = 0U; i < count; i++)
    {
        Steering_Update();
    }
}

void Servo_SendOnePulse(uint32 highTimeUs)
{
    uint32 lowTimeUs;

    highTimeUs = Servo_LimitPulseUs(highTimeUs);
    lowTimeUs = SERVO_PERIOD_US - highTimeUs;

    IfxPort_setPinHigh(SERVO_PORT, SERVO_PIN);
    delayUs(highTimeUs);

    IfxPort_setPinLow(SERVO_PORT, SERVO_PIN);
    delayLowUsWithMotorUpdate(lowTimeUs);
}

void Servo_HoldPosition(uint32 highTimeUs, uint32 holdTimeMs)
{
    uint32 count;
    uint32 i;

    count = holdTimeMs / 20U;

    if (count == 0U)
    {
        count = 1U;
    }

    for (i = 0U; i < count; i++)
    {
        Servo_SendOnePulse(highTimeUs);
    }
}
