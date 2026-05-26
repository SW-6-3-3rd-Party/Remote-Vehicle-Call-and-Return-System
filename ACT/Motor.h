#ifndef MOTOR_H_
#define MOTOR_H_

#include "Ifx_Types.h"

typedef enum
{
    MOTOR_STATE_STOP = 0,
    MOTOR_STATE_FORWARD,
    MOTOR_STATE_REVERSE
} MotorState;

/*
 * 초기화
 */
void MotorControl_Init(void);

/*
 * 상태 제어
 */
void MotorControl_SetState(MotorState state);
MotorState MotorControl_GetState(void);

void MotorControl_Forward(void);
void MotorControl_Reverse(void);
void MotorControl_Stop(void);
void MotorControl_Brake(void);
void MotorControl_Coast(void);

/*
 * 속도 제어
 * dutyPercent: 0 ~ 100
 */
void MotorControl_SetDutyPercent(uint32 dutyPercent);
uint32 MotorControl_GetDutyPercent(void);
void MotorControl_SetWheelDutyPercent(uint32 leftDutyPercent,
                                      uint32 rightDutyPercent);
uint32 MotorControl_GetLeftDutyPercent(void);
uint32 MotorControl_GetRightDutyPercent(void);

/*
 * 현재 상태 유지용 업데이트
 * MotorControl_Update() 1회 = 약 1ms
 */
void MotorControl_Update(void);

/*
 * 현재 상태를 holdTimeMs 동안 유지
 */
void MotorControl_UpdateForMs(uint32 holdTimeMs);

#endif /* MOTOR_H_ */
