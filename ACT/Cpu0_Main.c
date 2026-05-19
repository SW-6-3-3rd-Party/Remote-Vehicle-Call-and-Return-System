#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "Bsp.h"

#include "Motor.h"
#include "Servo.h"
#include "Encoder.h"
#include "ActCan.h"
#include "CanComm.h"

IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;

/*
 * 엔코더 기준
 * 예: 11PPR * 감속비 90 = 990
 */
#define ENCODER_PULSES_PER_REV      (990U)

/*
 * 바퀴 지름
 * 네가 말한 6.5cm 기준.
 */
#define PI_F                         (3.1415926f)
#define WHEEL_DIAMETER_M             (0.065f)
#define ENCODER_PULSES_PER_REV_F     (990.0f)

/*
 * 상태 송신 주기
 *
 * ACT -> MAIN ActStatusMsg는 100ms마다 송신한다.
 * MAIN은 alive_counter가 300ms 동안 증가하지 않으면
 * CAN Bus 1 통신 두절(EVT_CAN1_LOST)로 판단한다.
 */
#define CAN_STATUS_TX_PERIOD_MS     (100U)

/*
 * 서보 갱신 주기
 * Steering_Update() 1회가 약 20ms blocking pulse 출력
 */
#define STEERING_UPDATE_PERIOD_MS   (20U)

/*
 * Debug Watch 변수
 */
volatile sint32 g_debugEncoderCount = 0;
volatile uint32 g_debugPulsePerSecond = 0;
volatile uint32 g_debugRpm = 0;

volatile uint32 g_debugStatusTxCount = 0;
volatile uint32 g_debugRxCount = 0;
volatile uint32 g_debugInvalidRxCount = 0;

static uint32 g_canStatusTxTimerMs = 0U;
static uint32 g_steeringUpdateTimerMs = 0U;

static uint32 App_ConvertPulsePerSecondToKmhX100(uint32 pulsePerSecond)
{
    float wheelCircumferenceM;
    float speedMps;
    float speedKmh;
    float speedKmhX100;

    wheelCircumferenceM = PI_F * WHEEL_DIAMETER_M;

    /*
     * RPS = pulse/sec / pulses_per_rev
     * m/s = RPS * wheel_circumference
     * km/h = m/s * 3.6
     */
    speedMps = ((float)pulsePerSecond / ENCODER_PULSES_PER_REV_F) * wheelCircumferenceM;
    speedKmh = speedMps * 3.6f;

    /*
     * CAN에는 소수 둘째 자리까지 보내기 위해 km/h * 100으로 전송
     */
    speedKmhX100 = speedKmh * 100.0f;

    if (speedKmhX100 < 0.0f)
    {
        speedKmhX100 = 0.0f;
    }

    if (speedKmhX100 > 65535.0f)
    {
        speedKmhX100 = 65535.0f;
    }

    return (uint32)(speedKmhX100 + 0.5f);
}

/*
 * ActCan_Update100us()는 이름상 100us마다 호출하는 함수라서,
 * 현재 1ms 루프 안에서 10번 호출해 timeout 기준을 맞춘다.
 */
static void App_UpdateCanRxFor1ms(void)
{
    uint32 i;

    for (i = 0U; i < 10U; i++)
    {
        ActCan_Update100us();
    }
}

static uint8 App_GetCurrentGearStateForCan(void)
{
    /*
     * ActCan_GetGearState()
     * 0=P, 1=R, 2=N, 3=D
     */
    return (uint8)ActCan_GetGearState();
}

static uint8 App_GetCurrentSteeringStateForCan(void)
{
    SteeringState steeringState;

    steeringState = Steering_GetState();

    /*
     * Servo.h 기준:
     * STEERING_LEFT   = 0
     * STEERING_MIDDLE = 1
     * STEERING_RIGHT  = 2
     *
     * CAN Status 기준:
     * 0=LEFT, 1=FRONT/CENTER, 2=RIGHT
     */
    switch (steeringState)
    {
        case STEERING_LEFT:
            return ACT_STATUS_STEERING_LEFT;

        case STEERING_RIGHT:
            return ACT_STATUS_STEERING_RIGHT;

        case STEERING_MIDDLE:
        default:
            return ACT_STATUS_STEERING_CENTER;
    }
}

static void App_SendStatusIfNeeded(void)
{
    uint32 speedKmhX100;
    uint8 gearState;
    uint8 steeringState;

    g_canStatusTxTimerMs++;

    if (g_canStatusTxTimerMs < CAN_STATUS_TX_PERIOD_MS)
    {
        return;
    }

    g_canStatusTxTimerMs = 0U;

    /*
     * ActStatusMsg 0x200
     *
     * Byte 0~1 : speed_kmh_x100
     * Byte 2   : gear_state
     * Byte 3   : steering_state
     * Byte 4   : alive_counter
     * Byte 5   : crc8
     * Byte 6~7 : reserved
     */
    speedKmhX100 = App_ConvertPulsePerSecondToKmhX100(Encoder_GetPulsePerSecond());
    gearState = App_GetCurrentGearStateForCan();
    steeringState = App_GetCurrentSteeringStateForCan();

    CanComm_SendActStatus(speedKmhX100,
                          gearState,
                          steeringState);

    g_debugStatusTxCount++;
}

static void App_UpdateDebugValues(void)
{
    g_debugEncoderCount = Encoder_GetCount();
    g_debugPulsePerSecond = Encoder_GetPulsePerSecond();
    g_debugRpm = Encoder_GetRpm(ENCODER_PULSES_PER_REV);

    g_debugRxCount = ActCan_GetRxCount();
    g_debugInvalidRxCount = ActCan_GetInvalidCount();
}

/*
 * 기본 1ms 루프
 * MotorControl_Update()가 내부에서 약 1ms software PWM cycle을 수행한다.
 */
static void App_Update1ms(void)
{
    /*
     * 1. CAN 명령 수신 처리
     *    MAIN -> ACT 명령을 받으면 ActCan.c 내부에서
     *    accel / brake / gear / steering / control_mode / safety_override에 따라
     *    Motor, Steering 상태를 바꾼다.
     */
    App_UpdateCanRxFor1ms();

    /*
     * 2. 현재 모터 상태대로 PWM 1 cycle 출력
     */
    MotorControl_Update();

    /*
     * 3. 엔코더 속도/카운트 갱신
     */
    Encoder_Update1ms();

    /*
     * 4. 100ms마다 상태 CAN 송신
     *    ID 0x200
     */
    App_SendStatusIfNeeded();

    /*
     * 5. Watch 확인용 값 갱신
     */
    App_UpdateDebugValues();
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&g_cpuSyncEvent);
    IfxCpu_waitEvent(&g_cpuSyncEvent, 1);

    /*
     * Peripheral Init
     */
    MotorControl_Init();
    Steering_Init();
    Encoder_Init();

    /*
     * 중요:
     * 최종 통합에서는 ActCan_Init()만 호출한다.
     * CanComm_Init()은 호출하지 않는다.
     *
     * ActCan_Init()이 CAN0 Node0, P20.8/P20.7, P20.6 STB LOW,
     * RX filter, RX interrupt, TX buffer까지 모두 초기화해야 한다.
     */
    ActCan_Init();

    /*
     * 기본 속도
     */
    MotorControl_SetDutyPercent(70U);

    /*
     * 초기 안전 상태
     */
    MotorControl_Coast();
    Steering_Center();
    Encoder_ResetCount();

    while (1)
    {
        /*
         * 1ms 기본 루프
         */
        App_Update1ms();

        /*
         * 서보 조향 업데이트
         *
         * 주의:
         * 현재 Steering_Update()는 20ms 동안 blocking으로 서보 펄스를 만든다.
         * 그래서 모터 PWM이 잠깐 끊길 수 있다.
         * 실제 최종형에서는 서보도 GTM/TOM 하드웨어 PWM으로 빼는 게 맞다.
         */
        g_steeringUpdateTimerMs++;

        if (g_steeringUpdateTimerMs >= STEERING_UPDATE_PERIOD_MS)
        {
            g_steeringUpdateTimerMs = 0U;
            Steering_Update();
        }
    }
}
