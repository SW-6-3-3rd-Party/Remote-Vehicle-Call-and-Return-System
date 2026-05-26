#ifndef UDSDIAG_H_
#define UDSDIAG_H_

#include "Ifx_Types.h"

/*
 * ======================================================
 * ACT ECU UDS Diagnostic
 *
 * ACT logical address : 0x0010
 * CAN Request ID      : 0x700, MAIN/Tester -> ACT
 * CAN Response ID     : 0x708, ACT -> MAIN/Tester
 * CAN format          : Standard ID, CAN FD+BRS
 * Nominal/data phase  : 500 kbit/s / 2 Mbit/s
 *
 * Transport:
 *   ISO-TP Single Frame only
 *   CAN FD DLC 8 or 12
 *   Byte0 = payload length, 1~11
 *   Byte1~ = UDS payload
 *
 * IMPORTANT:
 *   Longest response is SID 0x19 with both DTCs active: 11 UDS bytes.
 *   No multi-frame is used.
 * ======================================================
 */

#define UDS_ACT_LOGICAL_ADDRESS          (0x0010U)

#define UDS_DIAG_REQ_CAN_ID              (0x700U)
#define UDS_DIAG_RES_CAN_ID              (0x708U)

#define UDS_CAN_REQUEST_DLC              (8U)
#define UDS_CAN_RESPONSE_MAX_DLC         (12U)

/*
 * Call once after ActCan_Init()
 */
void UdsDiag_Init(void);

/*
 * Call every 1ms.
 * Must be called before MotorControl_Update()
 * so diagnostic routines can control motor/servo.
 */
void UdsDiag_Update1ms(void);

/*
 * Called from ActCan RX ISR when CAN ID 0x700 is received.
 */
void UdsDiag_OnCanRequest(const uint8 data[8]);

#endif /* UDSDIAG_H_ */
