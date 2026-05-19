#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"

#include "BodyControl.h"

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

volatile uint32 g_debugBodyStep = 0U;

static void App_BodyTestCycle(void)
{
    /*
     * 10. 경적 2초
     */
    g_debugBodyStep = 10U;
    BodyControl_HornForMs(2000U);

    /*
     * 11. 충돌 방지: 25cm, 소리 없음
     */
    g_debugBodyStep = 11U;
    BodyControl_CollisionWarningForMs(25U, 3000U);

    /*
     * 12. 충돌 방지 1단계: 15cm, 느린 삐삐
     */
    g_debugBodyStep = 12U;
    BodyControl_CollisionWarningForMs(15U, 4000U);

    /*
     * 13. 충돌 방지 2단계: 8cm, 빠른 삐삐
     */
    g_debugBodyStep = 13U;
    BodyControl_CollisionWarningForMs(8U, 4000U);

    /*
     * 14. 충돌 방지 3단계: 3cm, 연속 삐---
     */
    g_debugBodyStep = 14U;
    BodyControl_CollisionWarningForMs(3U, 4000U);

    /*
     * 15. 전체 OFF 2초
     */
    g_debugBodyStep = 15U;
    BodyControl_AllOff();
    BodyControl_UpdateForMs(2000U);
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    BodyControl_Init();

    while (1)
    {
        App_BodyTestCycle();
    }
}
