#include "soad_cfg.h"
#include "pdur.h"
#include "doip.h"       // DoIP_ProcessRx 선언부
#include "pdur_cfg.h"    // PDU ID (PDUR_UDP_CONTROL_ID 등)


/* 🚀 래퍼 함수들 (이 파일 내부에서만 쓰이므로 static으로 은닉) */
static void PduR_RouteRx_Control(uint8_t* payload, uint16_t length) {
    PduR_RouteRx(PDUR_UDP_CONTROL_ID, payload, length);
}

static void PduR_RouteRx_SomeIp(uint8_t* payload, uint16_t length) {
    PduR_RouteRx(PDUR_SOMEIP_ID, payload, length);
}


/* 🚀 SoAd 라우팅 테이블 (포트 번호 -> 호출할 함수 매핑) */
const SoAd_RxPortMapType SoAd_RxPortTable[] = {
    { 5000,  PduR_RouteRx_Control },
    { 30492, PduR_RouteRx_SomeIp  },
    { 13400, DoIP_ProcessRx       }
};

/* 테이블 배열의 크기 계산 */
const uint16_t SOAD_RX_PORT_TABLE_SIZE = sizeof(SoAd_RxPortTable) / sizeof(SoAd_RxPortMapType);
