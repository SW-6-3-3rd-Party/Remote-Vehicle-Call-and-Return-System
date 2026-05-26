#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "Bsp.h"

#include "Motor.h"
#include "Servo.h"
#include "Encoder.h"
#include "ActCan.h"
#include "CanComm.h"
#include "PotAdc.h"
#include "UdsDiag.h"

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
 * ACT -> MAIN Drive Status는 50ms마다 송신한다.
 * 새 CAN 인터페이스에서는 status alive_counter/crc8을 사용하지 않는다.
 */
#define CAN_STATUS_TX_PERIOD_MS      (50U)

/*
 * 서보 갱신 주기
 * Steering_Update() 1회가 약 20ms blocking pulse 출력
 */
#define STEERING_UPDATE_PERIOD_MS    (20U)

/*
 * 조향 상태 판정 기준
 *
 * 현재 PotAdc_GetAngleDeg()는 CAN용 각도:
 *   55  = LEFT  max, 실제 -35deg
 *   90  = CENTER,   실제   0deg
 *   125 = RIGHT max, 실제 +35deg
 *
 * 따라서 80~100을 CENTER 범위로 본다.
 */
#define STEERING_CENTER_LOW_DEG      (80U)
#define STEERING_CENTER_HIGH_DEG     (100U)

/*
 * Debug Watch 변수
 */
volatile sint32 g_debugEncoderCount = 0;
volatile uint32 g_debugPulsePerSecond = 0;
volatile uint32 g_debugRpm = 0;

volatile uint32 g_debugStatusTxCount = 0;
volatile uint32 g_debugRxCount = 0;
volatile uint32 g_debugInvalidRxCount = 0;
volatile uint8 g_debugCanSteeringState = ACT_STATUS_STEERING_CENTER;
volatile uint8 g_debugCanSteeringAngleDeg = 90U;

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

static uint8 App_GetCurrentSteeringStateForCan(uint8 steeringAngleDeg)
{
    /*
     * PotAdc_GetAngleDeg() 기준:
     *   55  근처 = LEFT
     *   90  근처 = CENTER
     *   125 근처 = RIGHT
     */
    if (steeringAngleDeg < STEERING_CENTER_LOW_DEG)
    {
        return ACT_STATUS_STEERING_LEFT;
    }

    if (steeringAngleDeg > STEERING_CENTER_HIGH_DEG)
    {
        return ACT_STATUS_STEERING_RIGHT;
    }

    return ACT_STATUS_STEERING_CENTER;
}

static void App_SendStatusIfNeeded(void)
{
    uint32 speedKmhX100;
    uint8 gearState;
    uint8 steeringState;
    uint8 steeringAngleDeg;

    g_canStatusTxTimerMs++;

    if (g_canStatusTxTimerMs < CAN_STATUS_TX_PERIOD_MS)
    {
        return;
    }

    g_canStatusTxTimerMs = 0U;

    /*
     * ActStatusMsg
     *
     * Byte 0   : speed_kmh_x100_L
     * Byte 1   : speed_kmh_x100_H
     * Byte 2   : gear_state
     * Byte 3   : steering_state
     * Byte 4   : steering_angle
     * Byte 5   : reserved 0x00
     * Byte 6   : reserved 0x00
     * Byte 7   : reserved 0x00
     */
    speedKmhX100 = App_ConvertPulsePerSecondToKmhX100(Encoder_GetPulsePerSecond());
    gearState = App_GetCurrentGearStateForCan();
    steeringAngleDeg = PotAdc_GetAngleDeg();
    steeringState = App_GetCurrentSteeringStateForCan(steeringAngleDeg);

    CanComm_SendActStatus(speedKmhX100,
                          gearState,
                          steeringState,
                          steeringAngleDeg);

    g_debugCanSteeringState = steeringState;
    g_debugCanSteeringAngleDeg = steeringAngleDeg;
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
     * 1-1. UDS 진단 처리
     *
     * RoutineControl이 모터/서보 상태를 바꿀 수 있으므로
     * MotorControl_Update()보다 먼저 호출한다.
     *
     * UDS 요청은 ActCan RX ISR에서 UdsDiag_OnCanRequest()로 저장되고,
     * 실제 처리는 여기서 수행된다.
     */
    UdsDiag_Update1ms();

    /*
     * 2. 현재 모터 상태대로 PWM 1 cycle 출력
     */
    MotorControl_Update();

    /*
     * 3. 엔코더 속도/카운트 갱신
     */
    Encoder_Update1ms();

    /*
     * 4. A3 / AN37 가변저항 ADC 값 갱신
     */
    PotAdc_Update1ms();

    /*
     * 5. 50ms마다 상태 CAN 송신
     */
    App_SendStatusIfNeeded();

    /*
     * 6. Watch 확인용 값 갱신
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
    PotAdc_Init();

    /*
     * 중요:
     * 최종 통합에서는 ActCan_Init()만 호출한다.
     * CanComm_Init()은 호출하지 않는다.
     *
     * ActCan_Init()이 CAN0 Node0, P20.8/P20.7, P20.6 STB LOW,
     * RX filter, RX interrupt, TX buffer까지 모두 초기화한다.
     *
     * UDS도 같은 CAN0 Node0을 사용한다.
     */
    ActCan_Init();
    UdsDiag_Init();

    /*
     * 기본 속도
     */
    MotorControl_SetDutyPercent(90U);

    /*
     * 초기 안전 상태
     */
    MotorControl_Coast();
    Steering_Center();
    Steering_UpdateForMs(500U);
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
         * 현재 Steering_Update()는 blocking으로 서보 펄스를 만든다.
         * Servo.c의 LOW 구간에서 MotorControl_Update()를 반복 호출하여
         * 서보 유지 중에도 모터 PWM이 계속 출력되게 한다.
         * 실제 최종형에서는 모터/서보 모두 GTM/TOM 하드웨어 PWM으로 빼는 게 맞다.
         */
        g_steeringUpdateTimerMs++;

        if (g_steeringUpdateTimerMs >= STEERING_UPDATE_PERIOD_MS)
        {
            g_steeringUpdateTimerMs = 0U;
            Steering_Update();
        }
    }
}
