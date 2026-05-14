#include "Motor.h"

#include "Ifx_Types.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "Bsp.h"

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

/*
 * 처음 테스트용 기본 속도
 */
#define MOTOR_DEFAULT_DUTY_PERCENT    (30U)

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


static void delayUs(uint32 us)
{
    waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, us));
}


/* ======================================================
 * Low Level: PWM Pin
 * ====================================================== */

static void Motor_SetPwmHighBoth(void)
{
    IfxPort_setPinHigh(MOTOR_A_PWM_PORT, MOTOR_A_PWM_PIN);
    IfxPort_setPinHigh(MOTOR_B_PWM_PORT, MOTOR_B_PWM_PIN);
}

static void Motor_SetPwmLowBoth(void)
{
    IfxPort_setPinLow(MOTOR_A_PWM_PORT, MOTOR_A_PWM_PIN);
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
 * 양쪽 모터 PWM을 동시에 HIGH/LOW 처리
 * ====================================================== */

static void Motor_SendPwmOneCycleBoth(uint32 dutyPercent)
{
    uint32 highTimeUs;
    uint32 lowTimeUs;

    if (dutyPercent > 100U)
    {
        dutyPercent = 100U;
    }

    highTimeUs = (MOTOR_PWM_PERIOD_US * dutyPercent) / 100U;
    lowTimeUs = MOTOR_PWM_PERIOD_US - highTimeUs;

    if (highTimeUs > 0U)
    {
        Motor_SetPwmHighBoth();
        delayUs(highTimeUs);
    }

    if (lowTimeUs > 0U)
    {
        Motor_SetPwmLowBoth();
        delayUs(lowTimeUs);
    }
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
    if (dutyPercent > 100U)
    {
        dutyPercent = 100U;
    }

    g_dutyPercent = dutyPercent;
}

uint32 MotorControl_GetDutyPercent(void)
{
    return g_dutyPercent;
}

void MotorControl_Update(void)
{
    switch (g_motorState)
    {
        case MOTOR_STATE_FORWARD:
            Motor_SetBrakeOffBoth();
            Motor_SetDirectionForwardBoth();
            Motor_SendPwmOneCycleBoth(g_dutyPercent);
            break;

        case MOTOR_STATE_REVERSE:
            Motor_SetBrakeOffBoth();
            Motor_SetDirectionReverseBoth();
            Motor_SendPwmOneCycleBoth(g_dutyPercent);
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
