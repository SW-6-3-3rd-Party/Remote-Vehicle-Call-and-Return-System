#include "PotAdc.h"

#include "Ifx_Types.h"
#include "Evadc/Adc/IfxEvadc_Adc.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "Bsp.h"

/*
 * ======================================================
 * Potentiometer ADC Input
 *
 * Motor Shield 기준 A3에 가변저항 가운데 핀 연결.
 *
 * TC375 ShieldBuddy:
 *   A3 = AN37
 *   AN37 = EVADC Group 8 / Channel 5
 *   AN37 Port = P40.7
 *
 * 가변저항 배선:
 *   한쪽 끝   -> 3.3V
 *   반대쪽 끝 -> GND
 *   가운데    -> A3 / AN37
 *
 * 중요:
 *   기존 ACT 소스에서 ADC가 안 읽힌 핵심 원인은
 *   EVADC Queue0 설정이 부족해서 result.B.VF가 계속 0이 되었기 때문이다.
 *
 *   이 버전은 아래를 명시적으로 설정한다.
 *   - Queue0 request slot priority
 *   - Queue0 start mode
 *   - Queue0 gating always
 *   - Analog converter normal operation
 *
 * Steering calibration:
 *   raw 580 = LEFT  max = -35deg
 *   raw 925 = RIGHT max = +35deg
 *
 * CAN angle:
 *   physical -35deg -> CAN 55
 *   physical   0deg -> CAN 90
 *   physical +35deg -> CAN 125
 * ======================================================
 */

/* =========================
 * AN37 ADC Definition
 * ========================= */
#define POT_ADC_GROUP_ID          IfxEvadc_GroupId_8
#define POT_ADC_CHANNEL_ID        IfxEvadc_ChannelId_5
#define POT_ADC_PORT              (&MODULE_P40)
#define POT_ADC_PIN               (7U)

#define POT_ADC_MAX_RAW           (4095U)
#define POT_ADC_VREF_MV           (3300U)

/* =========================
 * Steering Calibration
 * ========================= */
#define POT_RAW_LEFT_MAX          (580U)
#define POT_RAW_RIGHT_MAX         (925U)

#define POT_PHYS_LEFT_DEG         (-35)
#define POT_PHYS_RIGHT_DEG        (35)
#define POT_PHYS_TOTAL_DEG        (70)

#define POT_CAN_CENTER_DEG        (90U)

/*
 * raw range:
 *   925 - 580 = 345
 */
#define POT_RAW_RANGE             (POT_RAW_RIGHT_MAX - POT_RAW_LEFT_MAX)

/*
 * 1ms마다 유효 샘플을 누적하고 8개 평균 반영
 */
#define POT_AVG_SAMPLE_COUNT      (8U)

static IfxEvadc_Adc         g_evadc;
static IfxEvadc_Adc_Group   g_adcGroup;
static IfxEvadc_Adc_Channel g_adcChannel;

static uint32 g_potRaw = 0U;
static uint32 g_potMv = 0U;
static uint32 g_potPercent = 0U;

/*
 * CAN용 각도:
 *   55~125
 *   90=center
 */
static uint8 g_potAngleDeg = POT_CAN_CENTER_DEG;

/*
 * 실제 물리 조향각:
 *   -35~+35
 */
static sint16 g_potSignedAngleDeg = 0;

static uint32 g_accumRaw = 0U;
static uint32 g_accumCount = 0U;

volatile uint32 g_debugPotRaw = 0U;
volatile uint32 g_debugPotMv = 0U;
volatile uint32 g_debugPotPercent = 0U;
volatile uint32 g_debugPotMinRaw = POT_ADC_MAX_RAW;
volatile uint32 g_debugPotMaxRaw = 0U;
volatile uint8  g_debugPotAngleDeg = POT_CAN_CENTER_DEG;
volatile sint16 g_debugPotSignedAngleDeg = 0;

volatile uint32 g_debugPotUpdateCallCount = 0U;
volatile uint32 g_debugPotValidSampleCount = 0U;
volatile uint32 g_debugPotNoResultCount = 0U;

static void PotAdc_DelayUs(uint32 us)
{
    waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, us));
}

static void PotAdc_InitAnalogPin(void)
{
    /*
     * AN37 = P40.7 analog input
     */
    IfxPort_setPinFunctionMode(POT_ADC_PORT,
                               POT_ADC_PIN,
                               IfxPort_PinFunctionMode_analog);
}

static uint32 PotAdc_RawToMv(uint32 raw)
{
    if (raw > POT_ADC_MAX_RAW)
    {
        raw = POT_ADC_MAX_RAW;
    }

    return (raw * POT_ADC_VREF_MV) / POT_ADC_MAX_RAW;
}

static uint32 PotAdc_RawToPercent(uint32 raw)
{
    if (raw > POT_ADC_MAX_RAW)
    {
        raw = POT_ADC_MAX_RAW;
    }

    return (raw * 100U) / POT_ADC_MAX_RAW;
}

/*
 * raw -> 실제 물리 조향각 변환
 *
 * raw 580 -> -35deg
 * raw 925 -> +35deg
 *
 * signed_angle = ((raw - 580) * 70 / 345) - 35
 */
static sint16 PotAdc_RawToSignedAngleDeg(uint32 raw)
{
    sint32 angle;
    sint32 numerator;
    sint32 denominator;

    if (raw <= POT_RAW_LEFT_MAX)
    {
        return (sint16)POT_PHYS_LEFT_DEG;
    }

    if (raw >= POT_RAW_RIGHT_MAX)
    {
        return (sint16)POT_PHYS_RIGHT_DEG;
    }

    numerator = (sint32)(raw - POT_RAW_LEFT_MAX) * (sint32)POT_PHYS_TOTAL_DEG;
    denominator = (sint32)POT_RAW_RANGE;

    /*
     * 반올림:
     *   + denominator/2
     */
    angle = ((numerator + (denominator / 2)) / denominator) + POT_PHYS_LEFT_DEG;

    if (angle < POT_PHYS_LEFT_DEG)
    {
        angle = POT_PHYS_LEFT_DEG;
    }

    if (angle > POT_PHYS_RIGHT_DEG)
    {
        angle = POT_PHYS_RIGHT_DEG;
    }

    return (sint16)angle;
}

/*
 * 실제 물리각 -35~+35를 기존 CAN 각도 체계로 변환.
 *
 * CAN angle:
 *   90=center
 *   55=left max
 *   125=right max
 */
static uint8 PotAdc_SignedAngleToCanAngleDeg(sint16 signedAngleDeg)
{
    sint32 canAngle;

    canAngle = (sint32)POT_CAN_CENTER_DEG + (sint32)signedAngleDeg;

    if (canAngle < 0)
    {
        canAngle = 0;
    }

    if (canAngle > 255)
    {
        canAngle = 255;
    }

    return (uint8)canAngle;
}

static boolean PotAdc_TryReadRaw(uint32* rawOut)
{
    Ifx_EVADC_G_RES result;

    /*
     * Queue 변환을 계속 밀어준다.
     */
    IfxEvadc_Adc_startQueue(&g_adcGroup,
                             IfxEvadc_RequestSource_queue0);

    result = IfxEvadc_Adc_getResult(&g_adcChannel);

    if (result.B.VF == 0U)
    {
        return FALSE;
    }

    *rawOut = (uint32)result.B.RESULT;
    return TRUE;
}

static void PotAdc_ApplyRaw(uint32 raw)
{
    sint16 signedAngleDeg;
    uint8 canAngleDeg;

    g_potRaw = raw;
    g_potMv = PotAdc_RawToMv(raw);
    g_potPercent = PotAdc_RawToPercent(raw);

    signedAngleDeg = PotAdc_RawToSignedAngleDeg(raw);
    canAngleDeg = PotAdc_SignedAngleToCanAngleDeg(signedAngleDeg);

    g_potSignedAngleDeg = signedAngleDeg;
    g_potAngleDeg = canAngleDeg;

    if (raw < g_debugPotMinRaw)
    {
        g_debugPotMinRaw = raw;
    }

    if (raw > g_debugPotMaxRaw)
    {
        g_debugPotMaxRaw = raw;
    }

    g_debugPotRaw = g_potRaw;
    g_debugPotMv = g_potMv;
    g_debugPotPercent = g_potPercent;
    g_debugPotSignedAngleDeg = g_potSignedAngleDeg;
    g_debugPotAngleDeg = g_potAngleDeg;
}

void PotAdc_Init(void)
{
    IfxEvadc_Adc_Config adcConfig;
    IfxEvadc_Adc_GroupConfig adcGroupConfig;
    IfxEvadc_Adc_ChannelConfig adcChannelConfig;

    PotAdc_InitAnalogPin();

    /*
     * EVADC Module Init
     */
    IfxEvadc_Adc_initModuleConfig(&adcConfig, &MODULE_EVADC);
    IfxEvadc_Adc_initModule(&g_evadc, &adcConfig);

    /*
     * EVADC Group 8 Init
     */
    IfxEvadc_Adc_initGroupConfig(&adcGroupConfig, &g_evadc);

    adcGroupConfig.groupId = POT_ADC_GROUP_ID;
    adcGroupConfig.master = POT_ADC_GROUP_ID;

    /*
     * 기존 코드에는 이것만 있었음.
     * 하지만 이것만으로는 Queue0 변환이 실제로 안정적으로 돌지 않아
     * VF가 계속 0으로 남을 수 있음.
     */
    adcGroupConfig.arbiter.requestSlotQueue0Enabled = TRUE;

    /*
     * 이번에 실제로 동작한 핵심 설정.
     */
    adcGroupConfig.queueRequest[0].requestSlotPrio =
        IfxEvadc_RequestSlotPriority_highest;

    adcGroupConfig.queueRequest[0].requestSlotStartMode =
        IfxEvadc_RequestSlotStartMode_cancelInjectRepeat;

    adcGroupConfig.queueRequest[0].triggerConfig.gatingMode =
        IfxEvadc_GatingMode_always;

    IfxEvadc_Adc_initGroup(&g_adcGroup, &adcGroupConfig);

    /*
     * EVADC Group 8 / Channel 5 = AN37
     */
    IfxEvadc_Adc_initChannelConfig(&adcChannelConfig, &g_adcGroup);

    adcChannelConfig.channelId = POT_ADC_CHANNEL_ID;
    adcChannelConfig.resultRegister = (IfxEvadc_ChannelResult)POT_ADC_CHANNEL_ID;

    IfxEvadc_Adc_initChannel(&g_adcChannel, &adcChannelConfig);

    /*
     * Queue0에 AN37 채널 추가.
     * REFILL로 계속 반복 변환.
     */
    IfxEvadc_Adc_addToQueue(&g_adcChannel,
                             IfxEvadc_RequestSource_queue0,
                             IFXEVADC_QUEUE_REFILL);

    /*
     * 기존 ACT 소스에서 빠져 있던 부분.
     * Analog converter를 normal operation으로 명시적으로 전환.
     */
    IfxEvadc_Adc_setAnalogConvertControl(&MODULE_EVADC,
                                          &g_adcGroup,
                                          IfxEvadc_AnalogConverterMode_normalOperation);

    PotAdc_DelayUs(10U);

    /*
     * 첫 변환 시작.
     */
    IfxEvadc_Adc_startQueue(&g_adcGroup,
                             IfxEvadc_RequestSource_queue0);

    g_potRaw = 0U;
    g_potMv = 0U;
    g_potPercent = 0U;
    g_potSignedAngleDeg = 0;
    g_potAngleDeg = POT_CAN_CENTER_DEG;

    g_accumRaw = 0U;
    g_accumCount = 0U;

    g_debugPotRaw = 0U;
    g_debugPotMv = 0U;
    g_debugPotPercent = 0U;
    g_debugPotMinRaw = POT_ADC_MAX_RAW;
    g_debugPotMaxRaw = 0U;
    g_debugPotSignedAngleDeg = 0;
    g_debugPotAngleDeg = POT_CAN_CENTER_DEG;

    g_debugPotUpdateCallCount = 0U;
    g_debugPotValidSampleCount = 0U;
    g_debugPotNoResultCount = 0U;
}

void PotAdc_Update1ms(void)
{
    uint32 raw;
    uint32 avgRaw;

    g_debugPotUpdateCallCount++;

    if (PotAdc_TryReadRaw(&raw) == FALSE)
    {
        g_debugPotNoResultCount++;
        return;
    }

    g_debugPotValidSampleCount++;

    g_accumRaw += raw;
    g_accumCount++;

    if (g_accumCount < POT_AVG_SAMPLE_COUNT)
    {
        return;
    }

    avgRaw = g_accumRaw / g_accumCount;

    g_accumRaw = 0U;
    g_accumCount = 0U;

    PotAdc_ApplyRaw(avgRaw);
}

uint32 PotAdc_GetRaw(void)
{
    return g_potRaw;
}

uint32 PotAdc_GetMv(void)
{
    return g_potMv;
}

uint32 PotAdc_GetPercent(void)
{
    return g_potPercent;
}

uint8 PotAdc_GetAngleDeg(void)
{
    return g_potAngleDeg;
}

sint16 PotAdc_GetSignedAngleDeg(void)
{
    return g_potSignedAngleDeg;
}
