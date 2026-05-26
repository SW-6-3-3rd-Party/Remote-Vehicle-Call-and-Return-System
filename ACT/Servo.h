#ifndef SERVO_H_
#define SERVO_H_

#include "Ifx_Types.h"

typedef enum
{
    STEERING_LEFT = 0,
    STEERING_MIDDLE,
    STEERING_RIGHT
} SteeringState;

typedef enum
{
    STEERING_KEY_NULL = 0,      /* No steering command: center immediately */
    STEERING_KEY_LEFT = 1,      /* 왼쪽 신호 유지: 왼쪽으로 연속 조향 */
    STEERING_KEY_RIGHT = 2      /* 오른쪽 신호 유지: 오른쪽으로 연속 조향 */
} SteeringKey;

/* Steering control */
void Steering_Init(void);
void Steering_SetState(SteeringState state);
SteeringState Steering_GetState(void);

/*
 * 연속 조향 제어용.
 * LEFT/RIGHT/NULL 명령을 받으면 다음 Steering_Update()에서 목표 pulse로 즉시 이동한다.
 * NULL이면 현재 각도 유지가 아니라 중앙으로 즉시 복귀한다.
 */
void Steering_SetKey(SteeringKey key);
SteeringKey Steering_GetKey(void);

void Steering_Center(void);
void Steering_SetPulseUs(uint32 pulseUs);
uint32 Steering_GetPulseUs(void);

/*
 * 현재 key 상태에 따라 목표 pulse를 즉시 반영하고 서보 펄스 1회 출력.
 * Steering_Update() 1회 = 약 20ms.
 * while(1) 안에서 계속 호출해야 서보 신호가 유지된다.
 */
void Steering_Update(void);

/* 현재 key 상태를 holdTimeMs 동안 유지하며 연속 조향 */
void Steering_UpdateForMs(uint32 holdTimeMs);

/* 직접 pulse 값을 줄 때 사용 */
void Servo_SendOnePulse(uint32 highTimeUs);
void Servo_HoldPosition(uint32 highTimeUs, uint32 holdTimeMs);

void initGtmTomPwm(void);
void fadeLED(void);

#endif /* SERVO_H_ */
