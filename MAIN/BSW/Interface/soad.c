#include "soad.h"
#include "pdur.h"
#include "pdur_cfg.h"
#include "soad_cfg.h"
#include "doip.h"
#include "pdur.h"
#include "tcp_txrx.h"
#include "Ifx_Lwip.h"
#include <stddef.h>

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
        doip_server_close();
    }
}

void SoAd_IfTransmit(uint16_t port, uint8_t* payload, uint16_t length)
{
    if (port == 13400)
        TcpSend(payload, length);
    else if (port == 5001)
    {
        ip_addr_t ip;
        IP4_ADDR(&ip, 192,168,1,100);
        UdpSend(&ip, port, payload, length);
    }
    else if (port == 5002)
    {
        ip_addr_t ip;
        IP4_ADDR(&ip, 192,168,20,2);
        UdpSend(&ip, port, payload, length);
    }


}
