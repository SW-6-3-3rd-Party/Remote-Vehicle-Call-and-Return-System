#ifndef BODY_CONTROL_H_
#define BODY_CONTROL_H_

#include "Ifx_Types.h"

void BodyControl_Init(void);

/* Headlamp RGB */
void BodyControl_HeadlampOn(void);
void BodyControl_HeadlampOff(void);
void BodyControl_SetHeadlamp(boolean on);

/* Brake Lamp */
void BodyControl_BrakeLampOn(void);
void BodyControl_BrakeLampOff(void);
void BodyControl_SetBrakeLamp(boolean on);

/* Turn Signal */
void BodyControl_LeftTurnOn(void);
void BodyControl_LeftTurnOff(void);
void BodyControl_SetLeftTurn(boolean on);

void BodyControl_RightTurnOn(void);
void BodyControl_RightTurnOff(void);
void BodyControl_SetRightTurn(boolean on);

/* Buzzer */
void BodyControl_HornForMs(uint32 holdTimeMs);
void BodyControl_CollisionWarningForMs(uint16 distanceCm, uint32 holdTimeMs);

/* All Off */
void BodyControl_AllOff(void);

/* 1ms update for turn signal blinking */
void BodyControl_Update1ms(void);

/* Blocking test update */
void BodyControl_UpdateForMs(uint32 holdTimeMs);

#endif /* BODY_CONTROL_H_ */
