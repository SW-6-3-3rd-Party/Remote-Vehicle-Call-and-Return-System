#ifndef CANCOMM_H_
#define CANCOMM_H_

#include "Ifx_Types.h"

/*
 * ACT Status CAN Protocol
 * CAN ID : 0x200, Standard ID, Classical CAN, DLC 8
 *
 * Byte 0~1 : speed_kmh_x100  uint16, Little Endian
 *            실제 속도[km/h] = speed_kmh_x100 / 100
 *
 * Byte 2   : gear_state
 *            0=P, 1=R, 2=N, 3=D
 *
 * Byte 3   : steering_state
 *            0=LEFT, 1=FRONT/CENTER, 2=RIGHT
 *
 * Byte 4   : alive_counter
 *            ACT ECU가 ActStatusMsg를 정상 송신할 때마다 1씩 증가
 *            255 다음에는 0으로 순환
 *
 * Byte 5   : crc8
 *            CRC-8/SAE-J1850 over Byte0~Byte4
 *
 * Byte 6~7 : reserved
 *            0x00
 *
 * CRC-8/SAE-J1850:
 *   Polynomial = 0x1D
 *   Initial    = 0xFF
 *   XorOut     = 0xFF
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
                           uint8 steeringState);

extern volatile uint32 g_debugCanTxCallCount;
extern volatile uint32 g_debugCanTxOkCount;
extern volatile uint32 g_debugCanTxBusyCount;
extern volatile uint32 g_debugCanLastStatus;

extern volatile uint8 g_debugActStatusAliveCounter;
extern volatile uint8 g_debugActStatusLastCrc;

#endif /* CANCOMM_H_ */
