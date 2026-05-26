#ifndef BODY_CAN_H_
#define BODY_CAN_H_

#include "Ifx_Types.h"

/*
 * BODY ECU CAN FD Bus 2 Protocol, 500 kbps nominal, no bit-rate switching
 *
 * RX Control command: MAIN -> Body
 * CAN ID : 0x110, Standard ID, CAN FD, DLC 6, Period 20ms
 * Byte 0 : headlamp                 0=OFF, 1=ON
 * Byte 1 : turn_signal              0=OFF, 1=LEFT, 2=RIGHT, 3=HAZARD
 * Byte 2 : brake_lamp               0=OFF, 1=ON
 * Byte 3 : horn                     0=OFF, 1=ON
 * Byte 4 : collision_warning        0=OFF, 1=ON
 * Byte 5 : control_mode             0=Standby, 1=Remote Drive, 2=Diagnostic
 *
 * TX Collision event: Body -> MAIN
 * CAN ID : 0x210, Standard ID, CAN FD, DLC 5, Event Message
 * Byte 0   : collision_event        0=Not occurred, 1=Occurred
 * Byte 1~4 : occurrence_time_ms     uint32, ms, Big Endian
 *                                      Byte1=MSB, Byte4=LSB
 *
 * TX Heartbeat: Body -> MAIN
 * CAN ID : 0x310, Standard ID, CAN FD, DLC 1, Period 200ms
 * Byte 0 : alive_counter            0~255 cyclic
 *
 * RX UDS request: MAIN -> Body
 * CAN ID : 0x710, Standard ID, CAN FD, DLC 8, CanTp Single Frame
 *
 * TX UDS response: Body -> MAIN
 * CAN ID : 0x718, Standard ID, CAN FD, DLC 8, CanTp Single Frame
 */

#define BODY_CAN_CMD_ID                    (0x110U)
#define BODY_CAN_COLLISION_EVENT_ID        (0x210U)
#define BODY_CAN_HEARTBEAT_ID              (0x310U)
#define BODY_CAN_UDS_REQ_ID                (0x710U)
#define BODY_CAN_UDS_RESP_ID               (0x718U)

#define BODY_CAN_CMD_DLC                   (6U)
#define BODY_CAN_COLLISION_EVENT_DLC       (5U)
#define BODY_CAN_HEARTBEAT_DLC             (1U)
#define BODY_CAN_UDS_DLC                   (8U)

#define BODY_CAN_OFF                       (0U)
#define BODY_CAN_ON                        (1U)

#define BODY_CAN_TURN_OFF                  (0U)
#define BODY_CAN_TURN_LEFT                 (1U)
#define BODY_CAN_TURN_RIGHT                (2U)
#define BODY_CAN_TURN_HAZARD               (3U)

#define BODY_CAN_CONTROL_STANDBY           (0U)
#define BODY_CAN_CONTROL_REMOTE_DRIVE      (1U)
#define BODY_CAN_CONTROL_DIAGNOSTIC        (2U)

/* UDS services used by the Body ECU single-frame diagnostic handler. */
#define BODY_UDS_SID_SESSION_CONTROL       (0x10U)
#define BODY_UDS_SID_READ_DID              (0x22U)
#define BODY_UDS_SID_READ_DTC              (0x19U)
#define BODY_UDS_SID_CLEAR_DTC             (0x14U)
#define BODY_UDS_SID_ROUTINE_CONTROL       (0x31U)

#define BODY_UDS_POSITIVE_RESPONSE_OFFSET  (0x40U)

#define BODY_UDS_SESSION_DEFAULT           (0x01U)
#define BODY_UDS_SESSION_EXTENDED          (0x03U)

#define BODY_UDS_DID_ECU_ID                (0xF190U)
#define BODY_UDS_DID_ULTRASONIC_DISTANCE   (0x0201U)
#define BODY_UDS_DID_TURN_SIGNAL_STATUS    (0x0202U)
#define BODY_UDS_DID_COLLISION_SWITCH      (0x0203U)
#define BODY_UDS_DID_COLLISION_WARNING     (0x0204U)

#define BODY_UDS_RID_BUZZER_TEST           (0x0200U)
#define BODY_UDS_RID_LED_ALL_TEST          (0x0201U)
#define BODY_UDS_RID_ULTRASONIC_TEST       (0x0202U)

void BodyCan_Init(void);

/*
 * Called every 1ms.
 * - RX command timeout monitoring
 * - pending UDS response processing
 * - pending collision event retry
 * - 200ms heartbeat 0x310 TX
 */
void BodyCan_Update1ms(void);

/*
 * Send a collision event.
 * occurrenceTimeMs is encoded as uint32 ms, Byte1=MSB and Byte4=LSB.
 */
void BodyCan_SendCollisionEvent(uint32 occurrenceTimeMs);

/*
 * Queue a collision event using the current BodyCan ms counter.
 */
void BodyCan_ReportCollisionOccurred(void);

uint32 BodyCan_GetRxCount(void);
uint32 BodyCan_GetInvalidCount(void);
uint32 BodyCan_GetCrcErrorCount(void);
uint32 BodyCan_GetAliveErrorCount(void);

uint8 BodyCan_GetHeadlamp(void);
uint8 BodyCan_GetTurnSignal(void);
uint8 BodyCan_GetBrakeLamp(void);
uint8 BodyCan_GetHorn(void);
uint8 BodyCan_GetCollisionWarningLamp(void);
uint8 BodyCan_GetControlMode(void);
uint8 BodyCan_GetRxAliveCounter(void);
uint8 BodyCan_GetTxAliveCounter(void);
uint32 BodyCan_GetTimeMs(void);

uint32 BodyCan_GetUdsRxCount(void);
uint32 BodyCan_GetUdsTxCount(void);
uint8 BodyCan_GetUdsSession(void);

extern volatile uint32 g_debugBodyCanRxCount;
extern volatile uint32 g_debugBodyCanInvalidCount;
extern volatile uint32 g_debugBodyCanCrcErrorCount;
extern volatile uint32 g_debugBodyCanAliveErrorCount;

/*
 * Legacy debug names kept for debugger watch compatibility.
 * These counters now describe heartbeat TX.
 */
extern volatile uint32 g_debugBodyCanStatusTxCallCount;
extern volatile uint32 g_debugBodyCanStatusTxOkCount;
extern volatile uint32 g_debugBodyCanStatusTxBusyCount;
extern volatile uint32 g_debugBodyCanEventTxCallCount;
extern volatile uint32 g_debugBodyCanEventTxOkCount;
extern volatile uint32 g_debugBodyCanEventTxBusyCount;
extern volatile uint32 g_debugBodyCanEventPending;
extern volatile uint32 g_debugBodyCanLastCollisionTimeMs;

extern volatile uint32 g_debugBodyCanUdsRxCount;
extern volatile uint32 g_debugBodyCanUdsTxCount;
extern volatile uint32 g_debugBodyCanUdsTxBusyCount;
extern volatile uint8 g_debugBodyCanUdsSession;
extern volatile uint8 g_debugBodyCanUdsLastSid;
extern volatile uint8 g_debugBodyCanUdsLastNrc;

extern volatile uint8 g_debugBodyCanRxAliveCounter;
extern volatile uint8 g_debugBodyCanTxAliveCounter;
extern volatile uint8 g_debugBodyCanLastRxCrc;
extern volatile uint8 g_debugBodyCanLastTxCrc;
extern volatile uint32 g_debugBodyCanLastTxStatus;

#endif /* BODY_CAN_H_ */
