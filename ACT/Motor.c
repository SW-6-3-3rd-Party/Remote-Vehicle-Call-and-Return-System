#include "Motor.h"

#include "Ifx_Types.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "Bsp.h"

#include "Encoder.h"

/*
 * ======================================================
 * TC375 ShieldBuddy + Arduino Motor Shield Rev3
 *
 * Motor Shield Channel A
 * D3  = PWM A       = TC375 P02.1
 * D9  = Brake A     = TC375 P02.7
 * D12 = Direction A = TC375 P10.1
 *
 * Motor Shield Channel B
 * D11 = PWM B       = TC375 P10.3
 * D8  = Brake B     = TC375 P02.6
 * D13 = Direction B = TC375 P10.2
 *
 * 현재 조건:
 * - 모터 2개 사용
 * - 한쪽 모터는 +, - 를 반대로 연결함
 * - 그래서 B 모터 방향을 소프트웨어에서 반대로 보정함
 * ======================================================
 */


/* =========================
 * Motor A Pin Definition
 * ========================= */
#define MOTOR_A_PWM_PORT          (&MODULE_P02)
#define MOTOR_A_PWM_PIN           (1U)

#define MOTOR_A_BRAKE_PORT        (&MODULE_P02)
#define MOTOR_A_BRAKE_PIN         (7U)

#define MOTOR_A_DIR_PORT          (&MODULE_P10)
#define MOTOR_A_DIR_PIN           (1U)


/* =========================
 * Motor B Pin Definition
 * ========================= */
#define MOTOR_B_PWM_PORT          (&MODULE_P10)
#define MOTOR_B_PWM_PIN           (3U)

#define MOTOR_B_BRAKE_PORT        (&MODULE_P02)
#define MOTOR_B_BRAKE_PIN         (6U)

#define MOTOR_B_DIR_PORT          (&MODULE_P10)
#define MOTOR_B_DIR_PIN           (2U)


/*
 * Software PWM
 * 1000us = 1ms = 1kHz
 */
#define MOTOR_PWM_PERIOD_US       (1000U)
#define MOTOR_ENCODER_POLL_STEP_US (50U)

/*
 * Default straight drive speed.
 * Channel B is treated as left wheel, channel A as right wheel.
 */
#define MOTOR_DEFAULT_DUTY_PERCENT    (90U)
#define MOTOR_LEFT_IS_CHANNEL_A       (0U)

/*
 * 방향 보정
 *
 * A 모터는 정상 배선 기준:
 *   DIR HIGH = 전진
 *
 * B 모터는 +, - 를 반대로 연결했다고 했으므로:
 *   DIR LOW = 전진
 *
 * 만약 전진 호출했는데 두 바퀴가 서로 반대로 돌면
 * MOTOR_B_FORWARD_DIR_HIGH 값을 1U로 바꾸면 됨.
 */
#define MOTOR_A_FORWARD_DIR_HIGH      (1U)
#define MOTOR_B_FORWARD_DIR_HIGH      (0U)


static MotorState g_motorState = MOTOR_STATE_STOP;
static uint32 g_dutyPercent = MOTOR_DEFAULT_DUTY_PERCENT;
static uint32 g_leftDutyPercent = MOTOR_DEFAULT_DUTY_PERCENT;
static uint32 g_rightDutyPercent = MOTOR_DEFAULT_DUTY_PERCENT;


static void delayUs(uint32 us)
{
    while (us >= MOTOR_ENCODER_POLL_STEP_US)
    {
        waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER,
                                                 MOTOR_ENCODER_POLL_STEP_US));
        Encoder_Poll();
        us -= MOTOR_ENCODER_POLL_STEP_US;
    }

    if (us > 0U)
    {
        waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, us));
        Encoder_Poll();
    }
}


/* ======================================================
 * Low Level: PWM Pin
 * ====================================================== */

static void Motor_SetPwmLowBoth(void)
{
    IfxPort_setPinLow(MOTOR_A_PWM_PORT, MOTOR_A_PWM_PIN);
    IfxPort_setPinLow(MOTOR_B_PWM_PORT, MOTOR_B_PWM_PIN);
}

static void MotorA_SetPwmHigh(void)
{
    IfxPort_setPinHigh(MOTOR_A_PWM_PORT, MOTOR_A_PWM_PIN);
}

static void MotorA_SetPwmLow(void)
{
    IfxPort_setPinLow(MOTOR_A_PWM_PORT, MOTOR_A_PWM_PIN);
}

static void MotorB_SetPwmHigh(void)
{
    IfxPort_setPinHigh(MOTOR_B_PWM_PORT, MOTOR_B_PWM_PIN);
}

static void MotorB_SetPwmLow(void)
{
    IfxPort_setPinLow(MOTOR_B_PWM_PORT, MOTOR_B_PWM_PIN);
}


/* ======================================================
 * Low Level: Brake Pin
 * Arduino Motor Shield Rev3:
 * Brake HIGH = brake ON
 * Brake LOW  = brake release
 * ====================================================== */

static void Motor_SetBrakeOnBoth(void)
{
    IfxPort_setPinHigh(MOTOR_A_BRAKE_PORT, MOTOR_A_BRAKE_PIN);
    IfxPort_setPinHigh(MOTOR_B_BRAKE_PORT, MOTOR_B_BRAKE_PIN);
}

static void Motor_SetBrakeOffBoth(void)
{
    IfxPort_setPinLow(MOTOR_A_BRAKE_PORT, MOTOR_A_BRAKE_PIN);
    IfxPort_setPinLow(MOTOR_B_BRAKE_PORT, MOTOR_B_BRAKE_PIN);
}


/* ======================================================
 * Low Level: Direction Pin
 * ====================================================== */

static void MotorA_SetDirectionForward(void)
{
#if MOTOR_A_FORWARD_DIR_HIGH
    IfxPort_setPinHigh(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
#else
    IfxPort_setPinLow(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
#endif
}

static void MotorA_SetDirectionReverse(void)
{
#if MOTOR_A_FORWARD_DIR_HIGH
    IfxPort_setPinLow(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
#else
    IfxPort_setPinHigh(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
#endif
}

static void MotorB_SetDirectionForward(void)
{
#if MOTOR_B_FORWARD_DIR_HIGH
    IfxPort_setPinHigh(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
#else
    IfxPort_setPinLow(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
#endif
}

static void MotorB_SetDirectionReverse(void)
{
#if MOTOR_B_FORWARD_DIR_HIGH
    IfxPort_setPinLow(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
#else
    IfxPort_setPinHigh(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
#endif
}

static void Motor_SetDirectionForwardBoth(void)
{
    MotorA_SetDirectionForward();
    MotorB_SetDirectionForward();
}

static void Motor_SetDirectionReverseBoth(void)
{
    MotorA_SetDirectionReverse();
    MotorB_SetDirectionReverse();
}


/* ======================================================
 * Software PWM 1 Cycle
 * Motor A/B can use different duty values for steering compensation.
 * ====================================================== */

static uint32 Motor_LimitDutyPercent(uint32 dutyPercent)
{
    if (dutyPercent > 100U)
    {
        dutyPercent = 100U;
    }

    return dutyPercent;
}

static void Motor_SendPwmOneCycleChannels(uint32 dutyA, uint32 dutyB)
{
    uint32 highTimeAUs;
    uint32 highTimeBUs;

    dutyA = Motor_LimitDutyPercent(dutyA);
    dutyB = Motor_LimitDutyPercent(dutyB);

    highTimeAUs = (MOTOR_PWM_PERIOD_US * dutyA) / 100U;
    highTimeBUs = (MOTOR_PWM_PERIOD_US * dutyB) / 100U;

    Motor_SetPwmLowBoth();

    if (highTimeAUs > 0U)
    {
        MotorA_SetPwmHigh();
    }

    if (highTimeBUs > 0U)
    {
        MotorB_SetPwmHigh();
    }

    if (highTimeAUs == highTimeBUs)
    {
        if (highTimeAUs > 0U)
        {
            delayUs(highTimeAUs);
        }

        Motor_SetPwmLowBoth();

        if (highTimeAUs < MOTOR_PWM_PERIOD_US)
        {
            delayUs(MOTOR_PWM_PERIOD_US - highTimeAUs);
        }
    }
    else if (highTimeAUs < highTimeBUs)
    {
        if (highTimeAUs > 0U)
        {
            delayUs(highTimeAUs);
        }

        MotorA_SetPwmLow();
        delayUs(highTimeBUs - highTimeAUs);
        MotorB_SetPwmLow();

        if (highTimeBUs < MOTOR_PWM_PERIOD_US)
        {
            delayUs(MOTOR_PWM_PERIOD_US - highTimeBUs);
        }
    }
    else
    {
        if (highTimeBUs > 0U)
        {
            delayUs(highTimeBUs);
        }

        MotorB_SetPwmLow();
        delayUs(highTimeAUs - highTimeBUs);
        MotorA_SetPwmLow();

        if (highTimeAUs < MOTOR_PWM_PERIOD_US)
        {
            delayUs(MOTOR_PWM_PERIOD_US - highTimeAUs);
        }
    }
}

static void Motor_SendPwmOneCycleWheels(uint32 leftDutyPercent,
                                        uint32 rightDutyPercent)
{
#if MOTOR_LEFT_IS_CHANNEL_A
    Motor_SendPwmOneCycleChannels(leftDutyPercent, rightDutyPercent);
#else
    Motor_SendPwmOneCycleChannels(rightDutyPercent, leftDutyPercent);
#endif
}


/* ======================================================
 * Public Functions
 * ====================================================== */

void MotorControl_Init(void)
{
    /* Motor A */
    IfxPort_setPinModeOutput(MOTOR_A_PWM_PORT,
                             MOTOR_A_PWM_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinModeOutput(MOTOR_A_BRAKE_PORT,
                             MOTOR_A_BRAKE_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinModeOutput(MOTOR_A_DIR_PORT,
                             MOTOR_A_DIR_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    /* Motor B */
    IfxPort_setPinModeOutput(MOTOR_B_PWM_PORT,
                             MOTOR_B_PWM_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinModeOutput(MOTOR_B_BRAKE_PORT,
                             MOTOR_B_BRAKE_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    IfxPort_setPinModeOutput(MOTOR_B_DIR_PORT,
                             MOTOR_B_DIR_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    g_motorState = MOTOR_STATE_STOP;
    g_dutyPercent = MOTOR_DEFAULT_DUTY_PERCENT;
    g_leftDutyPercent = MOTOR_DEFAULT_DUTY_PERCENT;
    g_rightDutyPercent = MOTOR_DEFAULT_DUTY_PERCENT;

    MotorControl_Stop();
}

void MotorControl_SetState(MotorState state)
{
    if ((state == MOTOR_STATE_STOP) ||
        (state == MOTOR_STATE_FORWARD) ||
        (state == MOTOR_STATE_REVERSE))
    {
        g_motorState = state;
    }
}

MotorState MotorControl_GetState(void)
{
    return g_motorState;
}

void MotorControl_Forward(void)
{
    MotorControl_SetState(MOTOR_STATE_FORWARD);
}

void MotorControl_Reverse(void)
{
    MotorControl_SetState(MOTOR_STATE_REVERSE);
}

void MotorControl_Stop(void)
{
    MotorControl_SetState(MOTOR_STATE_STOP);

    Motor_SetPwmLowBoth();
    Motor_SetBrakeOnBoth();
}

void MotorControl_Brake(void)
{
    MotorControl_SetState(MOTOR_STATE_STOP);

    Motor_SetPwmLowBoth();
    Motor_SetBrakeOnBoth();
}

void MotorControl_Coast(void)
{
    MotorControl_SetState(MOTOR_STATE_STOP);

    Motor_SetPwmLowBoth();
    Motor_SetBrakeOffBoth();
}

void MotorControl_SetDutyPercent(uint32 dutyPercent)
{
    dutyPercent = Motor_LimitDutyPercent(dutyPercent);

    g_dutyPercent = dutyPercent;
    g_leftDutyPercent = dutyPercent;
    g_rightDutyPercent = dutyPercent;
}

uint32 MotorControl_GetDutyPercent(void)
{
    return g_dutyPercent;
}

void MotorControl_SetWheelDutyPercent(uint32 leftDutyPercent,
                                      uint32 rightDutyPercent)
{
    leftDutyPercent = Motor_LimitDutyPercent(leftDutyPercent);
    rightDutyPercent = Motor_LimitDutyPercent(rightDutyPercent);

    g_leftDutyPercent = leftDutyPercent;
    g_rightDutyPercent = rightDutyPercent;

    if (leftDutyPercent == rightDutyPercent)
    {
        g_dutyPercent = leftDutyPercent;
    }
}

uint32 MotorControl_GetLeftDutyPercent(void)
{
    return g_leftDutyPercent;
}

uint32 MotorControl_GetRightDutyPercent(void)
{
    return g_rightDutyPercent;
}

void MotorControl_Update(void)
{
    switch (g_motorState)
    {
        case MOTOR_STATE_FORWARD:
            Motor_SetBrakeOffBoth();
            Motor_SetDirectionForwardBoth();
            Motor_SendPwmOneCycleWheels(g_leftDutyPercent,
                                        g_rightDutyPercent);
            break;

        case MOTOR_STATE_REVERSE:
            Motor_SetBrakeOffBoth();
            Motor_SetDirectionReverseBoth();
            Motor_SendPwmOneCycleWheels(g_leftDutyPercent,
                                        g_rightDutyPercent);
            break;

        case MOTOR_STATE_STOP:
        default:
            Motor_SetPwmLowBoth();
            Motor_SetBrakeOnBoth();
            delayUs(MOTOR_PWM_PERIOD_US);
            break;
    }
}

void MotorControl_UpdateForMs(uint32 holdTimeMs)
{
    uint32 i;

    /*
     * MotorControl_Update() 1회 = 약 1ms
     */
    for (i = 0U; i < holdTimeMs; i++)
    {
        MotorControl_Update();
    }
}
