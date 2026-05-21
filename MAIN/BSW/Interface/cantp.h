#ifndef CANTP_H
#define CANTP_H

#include <stdint.h>

/* CanIf가 수신 직후 호출해 줄 CanTp 수신 인터페이스 */
void CanTp_RxIndication(uint32_t canId, uint8_t* payload, uint8_t length);
void CanTp_Transmit(uint32_t canId, uint8_t* udsData, uint16_t udsLength);
#endif /* CANTP_H */
