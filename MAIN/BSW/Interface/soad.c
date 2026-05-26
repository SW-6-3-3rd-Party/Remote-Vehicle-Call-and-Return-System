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

err_t SoAd_TransmitMulticast(const char* ipStr, uint16_t destPort, const void* data, uint16_t length)
{
    ip_addr_t multicast_ip;
    ipaddr_aton(ipStr, &multicast_ip); // 문자열 IP를 lwIP 구조체로 변환

    /* 하위 드라이버단을 통해 멀티캐스트 전송 */
    return UdpSend(&multicast_ip, destPort, data, length);
}

void SoAd_IfTransmit(uint16_t port, uint8_t* payload, uint16_t length)
{
    //좀 꼬여서 수신 포트랑 송신 포트랑 둘 다 port로 써버림
    /* TCP (DoIP) */
    if (port == 13400)
    {
        TcpSend(payload, length);
    }
    /* UDP: PC 상태 보고 (5001 포트로 쏴라) */
    else if (port == 5001)
    {
        UdpSendToPC(5001, payload, length);
    }
    /* UDP: RPi 제어 (5002 포트로 쏴라) */
    else if (port == 5002)
    {
        UdpSendToRPi(5002, payload, length);
    }
    /* UDP: SOME/IP 응답 (30492 내 포트를 통해 PC에게 답장해라) */
    else if (port == 30492)
    {
        UdpSendSomeIpResponse(payload, length);
    }
}
