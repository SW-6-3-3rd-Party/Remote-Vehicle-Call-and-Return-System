#ifndef CANCOMM_H_
#define CANCOMM_H_

#include "Ifx_Types.h"
#include "Motor.h"

void CanComm_Init(void);

void CanComm_SendDriveStatus(MotorState state,
                             uint32 dutyPercent,
                             uint32 speedKmhX100,
                             uint32 pulsePerSecond,
                             sint32 encoderCount);

extern volatile uint32 g_debugCanTxCallCount;
extern volatile uint32 g_debugCanTxOkCount;
extern volatile uint32 g_debugCanTxBusyCount;
extern volatile uint32 g_debugCanLastStatus;

#endif /* CANCOMM_H_ */
