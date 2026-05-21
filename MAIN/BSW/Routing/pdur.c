#include "pdur_cfg.h"
#include "pdur.h"
#include <stddef.h> // NULL

void PduR_RouteRx(uint16_t rxPduId, uint8_t* payload, uint16_t length) {
    // 1. 라우팅 테이블을 처음부터 끝까지 검색
    for (uint16_t i = 0; i < PduR_Rx_RoutingTableSize; i++) {

        // 2. 입력된 PDU ID와 일치하는 경로를 찾으면
        if (PduR_Rx_RoutingTable[i].rxPduId == rxPduId) {

            // 3. 목적지 함수가 비어있지 않은지 확인 후 호출
            if (PduR_Rx_RoutingTable[i].targetFunc != NULL) {
                PduR_Rx_RoutingTable[i].targetFunc(payload, length);
            }
            return; // 라우팅 완료 후 종료
        }
    }

    // 예외 처리: 테이블에 없는 ID가 들어왔을 때 (디버그 로그 등)
}

void PduR_RouteTx(uint16_t txPduId, uint8_t* payload, uint16_t length) {
    // 1. 라우팅 테이블을 처음부터 끝까지 검색
    for (uint16_t i = 0; i < PduR_Tx_RoutingTableSize; i++) {

        // 2. 입력된 PDU ID와 일치하는 경로를 찾으면
        if (PduR_Tx_RoutingTable[i].txPduId == txPduId) {

            // 3. 목적지 함수가 비어있지 않은지 확인 후 호출
            if (PduR_Tx_RoutingTable[i].targetFunc != NULL) {
                PduR_Tx_RoutingTable[i].targetFunc(payload, length);
            }
            return; // 라우팅 완료 후 종료
        }
    }

    // 예외 처리: 테이블에 없는 ID가 들어왔을 때 (디버그 로그 등)
}
