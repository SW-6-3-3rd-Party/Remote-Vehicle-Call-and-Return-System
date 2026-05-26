#ifndef CANCOMM_H_
#define CANCOMM_H_

#include "Ifx_Types.h"
#include "IfxCan_Can.h"

/*
 * ======================================================
 * ACT status CAN protocol
 * ACT -> MAIN, period 50ms
 * CAN ID : 0x200, Standard ID, CAN FD+BRS, DLC 5
 * Nominal phase: 500 kbit/s, data phase: 2 Mbit/s
 *
 * Byte 0 : speed_kmh_x100_L   lower byte, little-endian, unit 0.01 km/h
 * Byte 1 : speed_kmh_x100_H   upper byte, little-endian
 * Byte 2 : gear_state         0=P, 1=R, 2=N, 3=D
 * Byte 3 : steering_state     0=LEFT, 1=CENTER, 2=RIGHT
 * Byte 4 : steering_angle     0~255, current steering angle
 *
 * NOTE:
 * - Byte4 steering_angle까지 보내려면 DLC는 5가 맞다.
 * - 문서 표에 DLC 4로 되어 있으면 Byte4는 전송 불가능하다.
 * ======================================================
 */
#define ACT_STATUS_CAN_ID                (0x200U)
#define ACT_STATUS_CAN_DLC               (5U)
#define ACT_STATUS_CAN_DLC_IFX           IfxCan_DataLengthCode_5

#define ACT_STATUS_GEAR_P                (0U)
#define ACT_STATUS_GEAR_R                (1U)
#define ACT_STATUS_GEAR_N                (2U)
#define ACT_STATUS_GEAR_D                (3U)

#define ACT_STATUS_STEERING_LEFT         (0U)
#define ACT_STATUS_STEERING_CENTER       (1U)
#define ACT_STATUS_STEERING_RIGHT        (2U)

void CanComm_Init(void);

void CanComm_SendActStatus(uint32 speedKmhX100,
                           uint8 gearState,
                           uint8 steeringState,
                           uint8 steeringAngleDeg);

extern volatile uint32 g_debugCanTxCallCount;
extern volatile uint32 g_debugCanTxOkCount;
extern volatile uint32 g_debugCanTxBusyCount;
extern volatile uint32 g_debugCanLastStatus;
extern volatile uint8 g_debugActStatusLastSteeringAngleDeg;

#endif /* CANCOMM_H_ */
