#include "pdur_cfg.h"
#include "com.h"
#include "canif.h"
#include "cantp.h"
#include "soad.h"
#include "doip.h"
#include "uds.h"

#include <stdint.h>


static void UDS_TCP_Main_ProcessRx(uint8_t* payload, uint16_t length)
{
    Uds_ProcessRx(payload, length);
}

static void UDS_TCP_Act_ProcessRx(uint8_t* payload, uint16_t length)
{
    CanTp_Transmit(0x700, payload, length);
}
static void UDS_TCP_Body_ProcessRx(uint8_t* payload, uint16_t length)
{
    CanTp_Transmit(0x710, payload, length);
}

static void CAN_Event_processRx(uint8_t* payload, uint16_t length)
{
    SoAd_IfTransmit(5002, payload, length);
}

static void CAN_Act_UDS_processRx(uint8_t* payload, uint16_t length)
{
    DoIP_ProcessTx(0x0010, payload, length);
}

static void CAN_Body_UDS_processRx(uint8_t* payload, uint16_t length)
{
    DoIP_ProcessTx(0x0020, payload, length);
}

static void CAN_Ctr_ActTx(uint8_t* payload, uint16_t length)
{
    CanIf_Transmit(0x100, payload, length);
}

static void CAN_Ctr_BodyTx(uint8_t* payload, uint16_t length)
{
    CanIf_Transmit(0x110, payload, length);
}

static void UDP_Stat_Tx(uint8_t* payload, uint16_t length)
{
    SoAd_IfTransmit(5001, payload, length);
}

/* 라우팅 테이블 정의 */
const PduR_Rx_RoutingPathType PduR_Rx_RoutingTable[] = {
    { PDUR_UDP_CONTROL_ID,      UDP_Ctr_ProcessRx        },
    { PDUR_SOMEIP_ID,           SomeIp_ProcessRx         },
    { PDUR_DOIP_MAIN_TCP_ID,    UDS_TCP_Main_ProcessRx   },
    { PDUR_DOIP_ACT_TCP_ID,     UDS_TCP_Act_ProcessRx    },
    { PDUR_DOIP_BODY_TCP_ID,    UDS_TCP_Body_ProcessRx   },
    { PDUR_CAN_STATUS_ID,       CAN_Stat_processRx       },
    { PDUR_CAN_EVENT_ID,        CAN_Event_processRx      },
    { PDUR_CAN_BODY_ID,         CAN_Body_processRx       },
    { PDUR_CAN_ACT_UDS_ID,      CAN_Act_UDS_processRx    },
    { PDUR_CAN_BODY_UDS_ID,     CAN_Body_UDS_processRx   },
    // 나중에 진단 응답이나 새 채널이 생기면 여기에만 추가하면 끝!
};

const PduR_Tx_RoutingPathType PduR_Tx_RoutingTable[] = {
    { PDUR_TX_CAN_ACT_ID,       CAN_Ctr_ActTx            },
    { PDUR_TX_CAN_BODY_ID,      CAN_Ctr_BodyTx           },
    { PDUR_TX_UDP_STAT_ID,      UDP_Stat_Tx              }
};

/* 테이블의 크기 계산 */
const uint16_t PduR_Rx_RoutingTableSize = sizeof(PduR_Rx_RoutingTable) / sizeof(PduR_Rx_RoutingPathType);
const uint16_t PduR_Tx_RoutingTableSize = sizeof(PduR_Tx_RoutingTable) / sizeof(PduR_Tx_RoutingPathType);
