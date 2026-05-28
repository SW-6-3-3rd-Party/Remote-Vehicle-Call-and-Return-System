#ifndef ACT_CAN_H_
#define ACT_CAN_H_

#include "Ifx_Types.h"
#include "IfxCan_Can.h"

/*
 * ACT command CAN protocol
 * CAN ID : 0x321, Standard ID, Classical CAN, DLC 8
 *
 * Byte 0 : accel_key     0=not pressed, 1=pressed
 * Byte 1 : steering_key  0=NULL/hold,   1=left,    2=right
 * Byte 2 : brake_key     0=not pressed, 1=pressed
 * Byte 3 : gear_state    0=P, 1=R, 2=N, 3=D
 * Byte 4~7 : reserved, send 0x00
 */
#define ACT_CAN_CMD_ID                (0x321U)
#define ACT_CAN_DLC                   (8U)

#define ACT_CAN_ACCEL_OFF             (0U)
#define ACT_CAN_ACCEL_ON              (1U)

#define ACT_CAN_STEERING_NULL         (0U)
#define ACT_CAN_STEERING_LEFT         (1U)
#define ACT_CAN_STEERING_RIGHT        (2U)

#define ACT_CAN_BRAKE_OFF             (0U)
#define ACT_CAN_BRAKE_ON              (1U)

#define ACT_CAN_GEAR_P                (0U)
#define ACT_CAN_GEAR_R                (1U)
#define ACT_CAN_GEAR_N                (2U)
#define ACT_CAN_GEAR_D                (3U)

typedef enum
{
    ACT_GEAR_STATE_P = ACT_CAN_GEAR_P,
    ACT_GEAR_STATE_R = ACT_CAN_GEAR_R,
    ACT_GEAR_STATE_N = ACT_CAN_GEAR_N,
    ACT_GEAR_STATE_D = ACT_CAN_GEAR_D
} ActGearState;

void ActCan_Init(void);

/*
 * Call every 100us in main loop.
 * Applies latest CAN command and performs timeout fail-safe.
 */
void ActCan_Update100us(void);

uint32 ActCan_GetRxCount(void);
uint32 ActCan_GetInvalidCount(void);
ActGearState ActCan_GetGearState(void);

IfxCan_Can_Node* ActCan_GetCanNode(void);

#endif /* ACT_CAN_H_ */
