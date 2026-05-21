#ifndef SOAD_H
#define SOAD_H

#include <stdint.h>

/* SoAd 초기화 (소켓 설정이나 상태 초기화가 필요할 경우) */
//void SoAd_Init(void);

/* LwIP의 수신 콜백 함수가 호출할 Rx Indication 함수 */
void SoAd_RxIndication(uint16_t port, uint8_t* payload, uint16_t length);
void SoAd_CloseSocket(uint16_t port);
void SoAd_IfTransmit(uint16_t port, uint8_t* payload, uint16_t length);
#endif /* SOAD_H */
