#ifndef SOAD_CFG_H
#define SOAD_CFG_H

#include <stdint.h>

/* 콜백 함수 포인터 타입 */
typedef void (*SoAd_RxCallbackFp)(uint8_t* payload, uint16_t length);

/* SoAd 포트 매핑 구조체 */
typedef struct {
    uint16_t port;
    SoAd_RxCallbackFp callback;
} SoAd_RxPortMapType;

/* 외부에서 참조할 수 있도록 extern 선언 */
extern const SoAd_RxPortMapType SoAd_RxPortTable[];
extern const uint16_t SOAD_RX_PORT_TABLE_SIZE;

#endif /* SOAD_CFG_H */
