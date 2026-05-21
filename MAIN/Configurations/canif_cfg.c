#include "canif_cfg.h"
#include "pdur.h"
#include "pdur_cfg.h"
#include "cantp.h"


/* 상위 모듈 함수 */
extern void CanTp_RxIndication(uint32_t canId, uint8_t* payload, uint8_t length); // CanTp 수신 함수

/* PduR로 넘기기 위한 래퍼 함수 (시그니처 매칭 및 PDU ID 주입) */
static void PduR_RouteRx_CanStatus(uint8_t* payload, uint8_t length) {
    PduR_RouteRx(PDUR_CAN_STATUS_ID, payload, length);
}

static void PduR_RouteRx_CanEvent(uint8_t* payload, uint8_t length) {
    PduR_RouteRx(PDUR_CAN_EVENT_ID, payload, length);
}

static void PduR_RouteRx_CanBody(uint8_t* payload, uint8_t length) {
    PduR_RouteRx(PDUR_CAN_EVENT_ID, payload, length);
}

static void CanTp_Act_RxIndication(uint8_t* payload, uint8_t length) {
    CanTp_RxIndication(0x708, payload, length);
}

static void CanTp_Body_RxIndication(uint8_t* payload, uint8_t length) {
    CanTp_RxIndication(0x718, payload, length);
}

/* CanIf 라우팅 테이블 */
const CanIf_RxMapType CanIf_RxTable[] = {
    // 1. 상태 수신 (예: CAN ID 0x100) -> PduR 직행 (이후 COM으로 전달됨)
    { 0x200, PduR_RouteRx_CanStatus },
    { 0x210, PduR_RouteRx_CanEvent  },
    { 0x310, PduR_RouteRx_CanBody   },
    // 2. UDS 진단 응답 (예: CAN ID 0x7E8) -> ⭐ CanTp로 전달 (조각 모음 필요!)
    { 0x708, CanTp_Act_RxIndication     },
    { 0x718, CanTp_Body_RxIndication    }
};

const uint16_t CANIF_RX_TABLE_SIZE = sizeof(CanIf_RxTable) / sizeof(CanIf_RxMapType);
