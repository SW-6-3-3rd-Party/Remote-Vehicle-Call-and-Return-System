#include "soad.h"
#include "pdur.h"
#include "pdur_cfg.h"
#include "soad_cfg.h"
#include "doip.h"
#include "pdur.h"
#include "tcp_txrx.h"
#include "udp_txrx.h"
#include "gettime.h"
#include <stddef.h>

/* 소켓 종료를 예약하기 위한 상태 변수들 */
static boolean  SoAd_IsClosePending = FALSE;
static uint32_t SoAd_CloseRequestTime = 0;

void SoAd_RxIndication(uint16_t port, uint8_t* payload, uint16_t length)
{
    /* 테이블을 돌며 매칭되는 포트의 상위 함수(PduR 또는 DoIP)를 직접 호출 */
    for (uint16_t i = 0; i < SOAD_RX_PORT_TABLE_SIZE; i++)
    {
        if (SoAd_RxPortTable[i].port == port)
        {
            if (SoAd_RxPortTable[i].callback != NULL)
            {
                SoAd_RxPortTable[i].callback(payload, length);
            }
            return;
        }
    }
}

void SoAd_CloseSocket(uint16_t port)
{
    if (port == 13400)
    {
        SoAd_IsClosePending = TRUE;
        SoAd_CloseRequestTime = Get_SystemTime_ms();
    }
}

/**
 * @brief 메인 루프에서 주기적으로 호출되며 예약된 종료를 실행하는 함수
 */
void SoAd_MainFunction(void)
{
    if (SoAd_IsClosePending == TRUE)
    {
        /* NACK 송신 지시 후 50ms가 경과했는지 논블로킹으로 확인 */
        if ((Get_SystemTime_ms() - SoAd_CloseRequestTime) >= 50)
        {
            /* 50ms가 지났으므로 LwIP 송신 큐가 다 비워졌다고 판단하고 진짜로 닫음 */
            doip_server_close();

            SoAd_IsClosePending = FALSE; /* 플래그 초기화 */
        }
    }
}

void SoAd_IfTransmit(uint16_t port, uint8_t* payload, uint16_t length)
{
    if (port == 13400)
        TcpSend(payload, length);
    else if (port == 5001)
    {
        UdpSendBack(port, payload, length);
    }
    else if (port == 5002)
    {
        ip_addr_t ip;
        IP4_ADDR(&ip, 192,168,20,2);
        UdpSend(&ip, port, payload, length);
    }


}
