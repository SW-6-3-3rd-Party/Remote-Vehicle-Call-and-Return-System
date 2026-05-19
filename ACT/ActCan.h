#ifndef ACT_CAN_H_
#define ACT_CAN_H_

#include "Ifx_Types.h"
#include "IfxCan_Can.h"

/*
 * ACT command CAN protocol
 * CAN ID : 0x100, Standard ID, Classical CAN, DLC 8
 *
 * Byte 0 : accel_key        0=not pressed, 1=pressed
 * Byte 1 : steering_key     0=NULL/center return, 1=left, 2=right
 * Byte 2 : brake_key        0=not pressed, 1=pressed
 * Byte 3 : gear_state       0=P, 1=R, 2=N, 3=D
 * Byte 4 : control_mode     0=Standby, 1=Remote Drive, 2=Diagnostic
 * Byte 5 : safety_override  0=Normal, 1=Force Stop
 * Byte 6 : alive_counter    0~255, increment every command frame
 * Byte 7 : crc8             CRC-8/SAE-J1850 over Byte0~Byte6
 *
 * CRC-8/SAE-J1850:
 *   Polynomial = 0x1D
 *   Initial    = 0xFF
 *   XorOut     = 0xFF
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

#define ACT_CAN_CONTROL_STANDBY       (0U)
#define ACT_CAN_CONTROL_REMOTE_DRIVE  (1U)
#define ACT_CAN_CONTROL_DIAGNOSTIC    (2U)

#define ACT_CAN_SAFETY_NORMAL         (0U)
#define ACT_CAN_SAFETY_FORCE_STOP     (1U)

typedef enum
{
    ACT_GEAR_STATE_P = ACT_CAN_GEAR_P,
    ACT_GEAR_STATE_R = ACT_CAN_GEAR_R,
    ACT_GEAR_STATE_N = ACT_CAN_GEAR_N,
    ACT_GEAR_STATE_D = ACT_CAN_GEAR_D
} ActGearState;

typedef enum
{
    ACT_CONTROL_MODE_STANDBY = ACT_CAN_CONTROL_STANDBY,
    ACT_CONTROL_MODE_REMOTE_DRIVE = ACT_CAN_CONTROL_REMOTE_DRIVE,
    ACT_CONTROL_MODE_DIAGNOSTIC = ACT_CAN_CONTROL_DIAGNOSTIC
} ActControlMode;

typedef enum
{
    ACT_SAFETY_OVERRIDE_NORMAL = ACT_CAN_SAFETY_NORMAL,
    ACT_SAFETY_OVERRIDE_FORCE_STOP = ACT_CAN_SAFETY_FORCE_STOP
} ActSafetyOverride;

void ActCan_Init(void);

/*
 * Call every 100us in main loop.
 * Applies latest CAN command and performs alive timeout fail-safe.
 */
void ActCan_Update100us(void);

uint32 ActCan_GetRxCount(void);
uint32 ActCan_GetInvalidCount(void);
uint32 ActCan_GetCrcErrorCount(void);
uint32 ActCan_GetAliveErrorCount(void);

ActGearState ActCan_GetGearState(void);
ActControlMode ActCan_GetControlMode(void);
ActSafetyOverride ActCan_GetSafetyOverride(void);
uint8 ActCan_GetAliveCounter(void);

IfxCan_Can_Node* ActCan_GetCanNode(void);

#endif /* ACT_CAN_H_ */
