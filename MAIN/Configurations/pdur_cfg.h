#ifndef BSW_CFG_H
#define BSW_CFG_H

#include <stdint.h>

/* 1. 수신 PDU ID 정의 (포트 번호나 CAN ID와 매핑될 고유 ID) */
#define PDUR_UDP_CONTROL_ID     0x01
#define PDUR_SOMEIP_ID          0x02
#define PDUR_DOIP_MAIN_TCP_ID   0x03
#define PDUR_DOIP_ACT_TCP_ID    0x04
#define PDUR_DOIP_BODY_TCP_ID   0x05

#define PDUR_CAN_STATUS_ID      0x11  // 하위 ECU의 일반 상태 메시지
#define PDUR_CAN_EVENT_ID       0x12  // 충격 감지 시
#define PDUR_CAN_BODY_ID        0x13  // 바디 생존 신고

#define PDUR_CAN_ACT_UDS_ID     0x17  // 조각 모음이 끝난 UDS 진단 메시지
#define PDUR_CAN_BODY_UDS_ID    0x18
#define PDUR_CAN_MAIN_UDS_ID    0x19

/* 1. 송신 PDU ID 정의 (포트 번호나 CAN ID와 매핑될 고유 ID) */
#define PDUR_TX_CAN_ACT_ID      0xA1
#define PDUR_TX_CAN_BODY_ID     0xA2

#define PDUR_TX_UDP_STAT_ID    0xB1

/* 2. 상위 계층으로 데이터를 올릴 콜백 함수 포인터 타입 정의 */
typedef void (*PduR_RxIndicationFp)(uint8_t* payload, uint16_t length);
/* 2. 하위 계층으로 데이터를 내릴 함수 포인터 타입 정의 */
typedef void (*PduR_TxFp)(uint8_t* payload, uint16_t length);
/* 3. 라우팅 테이블 구조체 정의 */
typedef struct {
    uint16_t rxPduId;               // 입력받은 PDU ID
    PduR_RxIndicationFp targetFunc; // 목적지(상위 계층) 함수
} PduR_Rx_RoutingPathType;

typedef struct {
    uint16_t txPduId;               // 입력받은 PDU ID
    PduR_TxFp targetFunc; // 목적지(하위 계층) 함수
} PduR_Tx_RoutingPathType;

/* 외부에서 라우팅 테이블을 볼 수 있게 extern 선언 */
extern const PduR_Rx_RoutingPathType PduR_Rx_RoutingTable[];
extern const PduR_Tx_RoutingPathType PduR_Tx_RoutingTable[];
extern const uint16_t PduR_Rx_RoutingTableSize;
extern const uint16_t PduR_Tx_RoutingTableSize;
#endif /* BSW_CFG_H */
