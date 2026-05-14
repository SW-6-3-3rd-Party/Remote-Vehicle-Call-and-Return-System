#include "Servo.h"

#include "Ifx_Types.h"
#include "Bsp.h"
#include "IfxPort.h"
#include "IfxStm.h"

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
#define SERVO_MIN_US      (1000U)
#define SERVO_MAX_US      (1400U)

/*
 * 조향 최대각/중앙값.
 */
#define STEERING_LEFT_US      (1000U)
#define STEERING_MIDDLE_US    (1200U)
#define STEERING_RIGHT_US     (1400U)

/*
 * 조향 이동 속도.
 * Steering_Update() 1회가 약 20ms이므로, 20ms마다 15us씩 이동한다.
 */
#define STEERING_STEP_US      (15U)

static SteeringState g_steeringState = STEERING_MIDDLE;
static SteeringKey g_steeringKey = STEERING_KEY_NULL;
static uint32 g_currentPulseUs = STEERING_MIDDLE_US;

static void delayUs(uint32 us)
{
    waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, us));
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
    if (g_currentPulseUs <= STEERING_LEFT_US)
    {
        g_steeringState = STEERING_LEFT;
    }
    else if (g_currentPulseUs >= STEERING_RIGHT_US)
    {
        g_steeringState = STEERING_RIGHT;
    }
    else
    {
        g_steeringState = STEERING_MIDDLE;
    }
}

static void Steering_MoveToward(uint32 targetPulseUs)
{
    targetPulseUs = Servo_LimitPulseUs(targetPulseUs);

    if (g_currentPulseUs < targetPulseUs)
    {
        if ((g_currentPulseUs + STEERING_STEP_US) < targetPulseUs)
        {
            g_currentPulseUs += STEERING_STEP_US;
        }
        else
        {
            g_currentPulseUs = targetPulseUs;
        }
    }
    else if (g_currentPulseUs > targetPulseUs)
    {
        if (g_currentPulseUs > (targetPulseUs + STEERING_STEP_US))
        {
            g_currentPulseUs -= STEERING_STEP_US;
        }
        else
        {
            g_currentPulseUs = targetPulseUs;
        }
    }
    else
    {
        /* 이미 목표 pulse면 아무것도 하지 않음 */
    }

    g_currentPulseUs = Servo_LimitPulseUs(g_currentPulseUs);
    Steering_UpdateStateFromPulse();
}

static void Steering_UpdatePulseByKey(void)
{
    switch (g_steeringKey)
    {
        case STEERING_KEY_LEFT:
            /* 왼쪽 신호 유지: 왼쪽 최대각까지 연속 이동, 이후에는 더 이상 이동하지 않음 */
            Steering_MoveToward(STEERING_LEFT_US);
            break;

        case STEERING_KEY_RIGHT:
            /* 오른쪽 신호 유지: 오른쪽 최대각까지 연속 이동, 이후에는 더 이상 이동하지 않음 */
            Steering_MoveToward(STEERING_RIGHT_US);
            break;

        case STEERING_KEY_NULL:
        default:
            /* 신호 없음: 현재 각도 유지가 아니라 같은 속도로 중앙 복귀 */
            Steering_MoveToward(STEERING_MIDDLE_US);
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
    /* 20ms마다 현재 key 상태에 맞춰 pulse를 한 스텝 이동 */
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
    delayUs(lowTimeUs);
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
