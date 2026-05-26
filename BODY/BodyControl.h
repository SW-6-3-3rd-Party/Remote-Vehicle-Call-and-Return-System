#ifndef BODY_CONTROL_H_
#define BODY_CONTROL_H_

#include "Ifx_Types.h"

void BodyControl_Init(void);

/* Lamp diagnosis fault/status mask bits */
#define BODY_DIAG_BRAKE_LAMP_BIT       (0x01U)  /* P00.0 feedback */
#define BODY_DIAG_HEADLAMP_BIT         (0x02U)  /* P00.2 feedback */
#define BODY_DIAG_LEFT_TURN_BIT        (0x04U)  /* P00.6 feedback */
#define BODY_DIAG_RIGHT_TURN_BIT       (0x08U)  /* P00.8 feedback */
#define BODY_DIAG_ULTRASONIC_BIT       (0x10U)  /* Ultrasonic trigger sent, no echo */


/* Headlamp RGB */
void BodyControl_HeadlampOn(void);
void BodyControl_HeadlampOff(void);
void BodyControl_SetHeadlamp(boolean on);

/* Brake Lamp */
void BodyControl_BrakeLampOn(void);
void BodyControl_BrakeLampOff(void);
void BodyControl_SetBrakeLamp(boolean on);

/* Turn Signal */
#define BODY_CONTROL_TURN_OFF       (0U)
#define BODY_CONTROL_TURN_LEFT      (1U)
#define BODY_CONTROL_TURN_RIGHT     (2U)
#define BODY_CONTROL_TURN_HAZARD    (3U)

void BodyControl_LeftTurnOn(void);
void BodyControl_LeftTurnOff(void);
void BodyControl_SetLeftTurn(boolean on);

void BodyControl_RightTurnOn(void);
void BodyControl_RightTurnOff(void);
void BodyControl_SetRightTurn(boolean on);

/*
 * turnMode:
 * 0=OFF, 1=LEFT, 2=RIGHT, 3=HAZARD
 */
void BodyControl_SetTurnSignal(uint8 turnMode);

/*
 * Collision warning lamp.
 * 별도 경고등 핀이 없어서 현재는 좌/우 방향지시등 동시 점멸로 표현한다.
 * 즉, collisionWarningLamp ON이면 Byte1 방향지시등보다 우선해서 양쪽이 깜빡인다.
 */
/* CAN Byte4 enables ultrasonic warning sound. Horn command has buzzer priority. */
void BodyControl_SetCollisionWarningLamp(boolean on);

/* Buzzer */
void BodyControl_SetHorn(boolean on);
void BodyControl_HornForMs(uint32 holdTimeMs);
void BodyControl_CollisionWarningForMs(uint16 distanceCm, uint32 holdTimeMs);

/* Collision event button: P33.10 */
boolean BodyControl_IsCollisionButtonPressed(void);
boolean BodyControl_ConsumeCollisionButtonPressedEvent(void);

/* All Off */
void BodyControl_AllOff(void);

/* 1ms update for turn signal blinking and non-blocking horn */
void BodyControl_Update1ms(void);

/* Blocking test update */
void BodyControl_UpdateForMs(uint32 holdTimeMs);

/* Lamp diagnosis */
void BodyControl_ClearDiagFaults(void);
uint8 BodyControl_GetDiagFaultMask(void);
uint8 BodyControl_GetDiagCommandMask(void);
uint8 BodyControl_GetDiagFeedbackMask(void);

uint16 BodyControl_GetUltrasonicDistanceCm(void);
boolean BodyControl_IsUltrasonicDistanceValid(void);
uint8 BodyControl_GetCollisionWarningLevel(void);

void BodyControl_RunBuzzerDiagnosticRoutine(void);
void BodyControl_RunLedDiagnosticRoutine(void);
boolean BodyControl_RunUltrasonicDiagnosticRoutine(uint16* distanceMm);

extern volatile uint8 g_debugBodyDiagFaultMask;
extern volatile uint8 g_debugBodyDiagCommandMask;
extern volatile uint8 g_debugBodyDiagFeedbackMask;
extern volatile uint8 g_debugBodyDiagStartupFaultMask;

extern volatile uint8 g_debugBodyCollisionButtonRawPressed;
extern volatile uint8 g_debugBodyCollisionButtonStablePressed;
extern volatile uint32 g_debugBodyCollisionButtonEventCount;

extern volatile uint16 g_debugBodyUltrasonicDistanceCm;
extern volatile uint8 g_debugBodyUltrasonicDistanceValid;
extern volatile uint8 g_debugBodyCollisionWarningLevel;
extern volatile uint32 g_debugBodyUltrasonicMeasureCount;
extern volatile uint32 g_debugBodyUltrasonicTimeoutCount;

#endif /* BODY_CONTROL_H_ */
