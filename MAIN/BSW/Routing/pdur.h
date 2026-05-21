#ifndef PDUR_H
#define PDUR_H

#include <stdint.h>

/* SoAd 등 하위 인터페이스에서 호출할 라우팅 엔진 함수 */
void PduR_RouteRx(uint16_t rxPduId, uint8_t* payload, uint16_t length);

/* (추후 확장용) 상위 계층에서 이더넷/CAN으로 데이터를 보낼 때 쓸 송신 라우팅 함수 */
void PduR_RouteTx(uint16_t txPduId, uint8_t* payload, uint16_t length);

#endif /* PDUR_H */
