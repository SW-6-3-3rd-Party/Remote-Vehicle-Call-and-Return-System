#include "Encoder.h"

#include "Ifx_Types.h"
#include "IfxPort.h"

/*
 * Yellow only encoder
 *
 * Yellow = Encoder A phase = P02.1
 */
#define ENC_A_PORT     (&MODULE_P02)
#define ENC_A_PIN      (4U)

#define ENCODER_SPEED_WINDOW_MS   (100U)

static sint32 g_encoderCount = 0;
static uint32 g_windowPulseCount = 0;
static uint32 g_windowRisingEdgeCount = 0;
static uint32 g_windowFallingEdgeCount = 0;
static uint32 g_pulsePerSecond = 0;
static uint32 g_elapsedMs = 0;

static uint8 g_lastA = 0U;

volatile uint32 g_debugEncoderPollCount = 0U;
volatile uint32 g_debugEncoderRawA = 0U;
volatile uint32 g_debugEncoderLastA = 0U;
volatile uint32 g_debugEncoderRisingEdgeCount = 0U;
volatile uint32 g_debugEncoderFallingEdgeCount = 0U;
volatile uint32 g_debugEncoderTransitionCount = 0U;
volatile uint32 g_debugEncoderWindowPulseCount = 0U;
volatile uint32 g_debugEncoderWindowRisingEdgeCount = 0U;
volatile uint32 g_debugEncoderWindowFallingEdgeCount = 0U;
volatile uint32 g_debugEncoderElapsedMs = 0U;
volatile uint32 g_debugEncoderPulsePerSecondInternal = 0U;

static uint8 Encoder_ReadA(void)
{
    return (IfxPort_getPinState(ENC_A_PORT, ENC_A_PIN) != FALSE) ? 1U : 0U;
}

void Encoder_Init(void)
{
    IfxPort_setPinModeInput(ENC_A_PORT,
                            ENC_A_PIN,
                            IfxPort_InputMode_pullUp);

    g_encoderCount = 0;
    g_windowPulseCount = 0;
    g_windowRisingEdgeCount = 0U;
    g_windowFallingEdgeCount = 0U;
    g_pulsePerSecond = 0;
    g_elapsedMs = 0;

    g_lastA = Encoder_ReadA();

    g_debugEncoderPollCount = 0U;
    g_debugEncoderRawA = g_lastA;
    g_debugEncoderLastA = g_lastA;
    g_debugEncoderRisingEdgeCount = 0U;
    g_debugEncoderFallingEdgeCount = 0U;
    g_debugEncoderTransitionCount = 0U;
    g_debugEncoderWindowPulseCount = 0U;
    g_debugEncoderWindowRisingEdgeCount = 0U;
    g_debugEncoderWindowFallingEdgeCount = 0U;
    g_debugEncoderElapsedMs = 0U;
    g_debugEncoderPulsePerSecondInternal = 0U;
}

void Encoder_Poll(void)
{
    uint8 nowA;

    nowA = Encoder_ReadA();
    g_debugEncoderPollCount++;
    g_debugEncoderRawA = nowA;
    g_debugEncoderLastA = g_lastA;

    if (nowA != g_lastA)
    {
        g_encoderCount++;
        g_debugEncoderTransitionCount++;
    }

    /*
     * Rising/falling을 따로 세고, 속도 계산 시 둘 중 큰 값을 사용한다.
     * 두 edge가 모두 잡혀도 pulse를 두 배로 세지 않기 위한 구조다.
     */
    if ((g_lastA == 0U) && (nowA == 1U))
    {
        g_windowRisingEdgeCount++;
        g_debugEncoderRisingEdgeCount++;
    }
    else if ((g_lastA == 1U) && (nowA == 0U))
    {
        g_windowFallingEdgeCount++;
        g_debugEncoderFallingEdgeCount++;
    }

    g_windowPulseCount = (g_windowRisingEdgeCount >= g_windowFallingEdgeCount) ?
                         g_windowRisingEdgeCount :
                         g_windowFallingEdgeCount;

    g_lastA = nowA;
    g_debugEncoderLastA = g_lastA;
    g_debugEncoderWindowPulseCount = g_windowPulseCount;
    g_debugEncoderWindowRisingEdgeCount = g_windowRisingEdgeCount;
    g_debugEncoderWindowFallingEdgeCount = g_windowFallingEdgeCount;
}

void Encoder_Update1ms(void)
{
    Encoder_Poll();

    /*
     * 100ms마다 속도 갱신
     */
    g_elapsedMs++;
    g_debugEncoderElapsedMs = g_elapsedMs;

    if (g_elapsedMs >= ENCODER_SPEED_WINDOW_MS)
    {
        g_pulsePerSecond = (g_windowPulseCount * 1000U) / g_elapsedMs;
        g_debugEncoderPulsePerSecondInternal = g_pulsePerSecond;

        g_windowPulseCount = 0U;
        g_windowRisingEdgeCount = 0U;
        g_windowFallingEdgeCount = 0U;
        g_elapsedMs = 0U;
        g_debugEncoderWindowPulseCount = 0U;
        g_debugEncoderWindowRisingEdgeCount = 0U;
        g_debugEncoderWindowFallingEdgeCount = 0U;
        g_debugEncoderElapsedMs = 0U;
    }
}

sint32 Encoder_GetCount(void)
{
    return g_encoderCount;
}

void Encoder_ResetCount(void)
{
    g_encoderCount = 0;
    g_windowPulseCount = 0U;
    g_windowRisingEdgeCount = 0U;
    g_windowFallingEdgeCount = 0U;
    g_pulsePerSecond = 0U;
    g_elapsedMs = 0U;

    g_debugEncoderRisingEdgeCount = 0U;
    g_debugEncoderFallingEdgeCount = 0U;
    g_debugEncoderTransitionCount = 0U;
    g_debugEncoderWindowPulseCount = 0U;
    g_debugEncoderWindowRisingEdgeCount = 0U;
    g_debugEncoderWindowFallingEdgeCount = 0U;
    g_debugEncoderElapsedMs = 0U;
    g_debugEncoderPulsePerSecondInternal = 0U;
}

uint32 Encoder_GetPulsePerSecond(void)
{
    return g_pulsePerSecond;
}

uint32 Encoder_GetRpm(uint32 pulsesPerRev)
{
    uint32 rpm;

    if (pulsesPerRev == 0U)
    {
        return 0U;
    }

    /*
     * RPM = pulse/sec * 60 / pulse/rev
     */
    rpm = (g_pulsePerSecond * 60U) / pulsesPerRev;

    return rpm;
}
