#ifndef CANCOMM_H_
#define CANCOMM_H_

#include "Ifx_Types.h"

/*
 * ======================================================
 * ACT status CAN protocol
 * ACT -> MAIN, period 50ms
 * CAN ID : 0x322, Standard ID, Classical CAN, DLC 8
 *
 * Byte 0 : speed_kmh_x100_L   lower byte, little-endian, unit 0.01 km/h
 * Byte 1 : speed_kmh_x100_H   upper byte, little-endian
 * Byte 2 : gear_state         0=P, 1=R, 2=N, 3=D
 * Byte 3 : steering_state     0=LEFT, 1=CENTER, 2=RIGHT
 * Byte 4 : steering_angle     0~255, current steering angle
 * Byte 5 : reserved           0x00
 * Byte 6 : reserved           0x00
 * Byte 7 : reserved           0x00
 *
 * IMPORTANT:
 * - This interface does NOT use alive_counter in Byte6.
 * - This interface does NOT use crc8 in Byte7.
 * ======================================================
 */
#define ACT_STATUS_CAN_ID                (0x322U)
#define ACT_STATUS_CAN_DLC               (8U)

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
