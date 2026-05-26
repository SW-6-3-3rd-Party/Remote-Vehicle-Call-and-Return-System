#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxStm.h"
#include "Bsp.h"

#include "BodyControl.h"
#include "BodyCan.h"

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

volatile uint32 g_debugBodyMainLoopCount = 0U;
volatile uint32 g_debugBodyCollisionEventRequestCount = 0U;

/*
 * BODY ECU main loop.
 *
 * - BodyControl_Init()
 *   Initializes lamp/buzzer GPIO, diagnosis feedback inputs, and collision button.
 *
 * - BodyCan_Init()
 *   RX 0x110 MAIN -> Body control command
 *   TX 0x210 Body -> MAIN collision event
 *   TX 0x310 Body -> MAIN heartbeat
 *   RX 0x710 MAIN -> Body UDS request, CanTp Single Frame
 *   TX 0x718 Body -> MAIN UDS response, CanTp Single Frame
 *
 * - 1ms loop
 *   Updates CAN timeout/heartbeat, output blinking/buzzer, and collision button edge.
 */
static void App_Delay1ms(void)
{
    waitTime(IfxStm_getTicksFromMicroseconds(BSP_DEFAULT_TIMER, 1000U));
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    BodyControl_Init();
    BodyCan_Init();

    while (1)
    {
        BodyCan_Update1ms();
        BodyControl_Update1ms();

        if (BodyControl_ConsumeCollisionButtonPressedEvent() != FALSE)
        {
            BodyCan_ReportCollisionOccurred();
            g_debugBodyCollisionEventRequestCount++;
        }

        g_debugBodyMainLoopCount++;

        App_Delay1ms();
    }
}
