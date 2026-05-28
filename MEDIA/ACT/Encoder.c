#include "Encoder.h"

#include "Ifx_Types.h"
#include "IfxPort.h"

/*
 * Yellow only encoder
 *
 * Yellow = Encoder A phase = D2 = P02.0
 */
#define ENC_A_PORT     (&MODULE_P02)
#define ENC_A_PIN      (0U)

#define ENCODER_SPEED_WINDOW_MS   (100U)

static sint32 g_encoderCount = 0;
static uint32 g_windowPulseCount = 0;
static uint32 g_pulsePerSecond = 0;
static uint32 g_elapsedMs = 0;

static uint8 g_lastA = 0U;

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
    g_pulsePerSecond = 0;
    g_elapsedMs = 0;

    g_lastA = Encoder_ReadA();
}

void Encoder_Update1ms(void)
{
    uint8 nowA;

    nowA = Encoder_ReadA();

    /*
     * Yellow rising edge만 카운트
     */
    if ((g_lastA == 0U) && (nowA == 1U))
    {
        g_encoderCount++;
        g_windowPulseCount++;
    }

    g_lastA = nowA;

    /*
     * 100ms마다 속도 갱신
     */
    g_elapsedMs++;

    if (g_elapsedMs >= ENCODER_SPEED_WINDOW_MS)
    {
        g_pulsePerSecond = (g_windowPulseCount * 1000U) / g_elapsedMs;

        g_windowPulseCount = 0U;
        g_elapsedMs = 0U;
    }
}

sint32 Encoder_GetCount(void)
{
    return g_encoderCount;
}

void Encoder_ResetCount(void)
{
    g_encoderCount = 0;
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
