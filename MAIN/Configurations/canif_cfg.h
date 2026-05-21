#ifndef CANIF_CFG_H
#define CANIF_CFG_H

#include <stdint.h>

/* CanIf 상위 콜백 함수 포인터 (payload와 length를 받음) */
typedef void (*CanIf_RxCallbackFp)(uint8_t* payload, uint8_t length);

/* CAN ID 매핑 구조체 */
typedef struct {
    uint32_t canId;               // 수신된 CAN ID (예: 0x100, 0x7E8)
    CanIf_RxCallbackFp callback;  // 라우팅할 상위 모듈 함수
} CanIf_RxMapType;

extern const CanIf_RxMapType CanIf_RxTable[];
extern const uint16_t CANIF_RX_TABLE_SIZE;

#endif /* CANIF_CFG_H */
