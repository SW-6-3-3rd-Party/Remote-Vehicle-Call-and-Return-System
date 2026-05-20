#ifndef POTADC_H_
#define POTADC_H_

#include "Ifx_Types.h"

/*
 * A3 / AN37 Potentiometer ADC
 *
 * Arduino Motor Shield 기준:
 *   A3 = AN37 = EVADC Group 8 / Channel 5 = P40.7
 *
 * Steering calibration:
 *   raw 580 = LEFT  max = -35deg
 *   raw 925 = RIGHT max = +35deg
 *
 * CAN angle convention:
 *   55  = LEFT  max, physical -35deg
 *   90  = CENTER,   physical   0deg
 *   125 = RIGHT max, physical +35deg
 */

void PotAdc_Init(void);
void PotAdc_Update1ms(void);

uint32 PotAdc_GetRaw(void);
uint32 PotAdc_GetMv(void);
uint32 PotAdc_GetPercent(void);

/*
 * CAN 전송용 각도.
 * 90deg = center 기준.
 * raw 580 -> 55
 * raw 752 -> 90
 * raw 925 -> 125
 */
uint8 PotAdc_GetAngleDeg(void);

/*
 * 실제 물리 조향각.
 * raw 580 -> -35
 * raw 752 -> 0
 * raw 925 -> +35
 */
sint16 PotAdc_GetSignedAngleDeg(void);

extern volatile uint32 g_debugPotRaw;
extern volatile uint32 g_debugPotMv;
extern volatile uint32 g_debugPotPercent;
extern volatile uint32 g_debugPotMinRaw;
extern volatile uint32 g_debugPotMaxRaw;

/*
 * CAN용 각도: 55~125, 90=center
 */
extern volatile uint8 g_debugPotAngleDeg;

/*
 * 실제 물리각: -35~+35
 */
extern volatile sint16 g_debugPotSignedAngleDeg;

extern volatile uint32 g_debugPotUpdateCallCount;
extern volatile uint32 g_debugPotValidSampleCount;
extern volatile uint32 g_debugPotNoResultCount;

#endif /* POTADC_H_ */
