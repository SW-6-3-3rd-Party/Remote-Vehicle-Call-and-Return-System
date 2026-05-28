#ifndef ENCODER_H_
#define ENCODER_H_

#include "Ifx_Types.h"

void Encoder_Init(void);

/*
 * 1ms마다 한 번씩 호출한다고 가정
 */
void Encoder_Update1ms(void);

sint32 Encoder_GetCount(void);
void Encoder_ResetCount(void);

/*
 * 초당 펄스 수
 */
uint32 Encoder_GetPulsePerSecond(void);

/*
 * RPM 계산용
 * pulsesPerRev는 출력축 1바퀴당 Yellow rising edge 펄스 수
 */
uint32 Encoder_GetRpm(uint32 pulsesPerRev);

#endif /* ENCODER_H_ */
