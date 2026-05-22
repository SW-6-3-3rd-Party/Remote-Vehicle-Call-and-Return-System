#include "udp_txrx.h"
#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "soad.h"

#include <string.h>

#if LWIP_UDP

#define CTR_PCB 5000
#define SOMEIP_PCB 30492

static struct udp_pcb *udp_send_pcb;
static struct udp_pcb *udp_ctr_pcb;
static struct udp_pcb *udp_someip_pcb;

static ip_addr_t *udp_addr;

static void udp_receive_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                             const ip_addr_t *addr, u16_t port)
{
    LWIP_UNUSED_ARG(arg);
    if (p != NULL)
    {
        /* SoAd 계층으로 데이터 전달 */
        udp_addr = addr;
        SoAd_RxIndication(upcb->local_port, (uint8_t*)p->payload, p->len);

        /* 메모리 누수 방지를 위해 pbuf 반드시 해제 */
        pbuf_free(p);
    }
}

void UdpInit(void)
{
    //송신 전용
    udp_send_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);


    udp_ctr_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (udp_ctr_pcb != NULL) {
        err_t err;
        err = udp_bind(udp_ctr_pcb, IP_ANY_TYPE, CTR_PCB);
        if (err == ERR_OK) {
            udp_recv(udp_ctr_pcb, udp_receive_recv, NULL);
        }
    }

    udp_someip_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (udp_someip_pcb != NULL) {
        err_t err;
        err = udp_bind(udp_someip_pcb, IP_ANY_TYPE, SOMEIP_PCB);
        if (err == ERR_OK) {
            udp_recv(udp_someip_pcb, udp_receive_recv, NULL);
        }
    }
}

/* ------------------------------------------------------------------ */
/* UDP 송신 함수                                                       */
/* ------------------------------------------------------------------ */
err_t UdpSend(const ip_addr_t *dst_addr, u16_t dst_port,
              const void *data, u16_t len)
{
    struct pbuf *p;
    err_t err;

    if (udp_send_pcb == NULL || dst_addr == NULL || data == NULL || len == 0) {
        return ERR_ARG;
    }

    /* PBUF_RAM: payload를 새 버퍼에 복사해 둠 (호출자가 data 버퍼를 즉시 재사용 가능) */
    p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL) {
        return ERR_MEM;
    }

    /* 사용자 데이터를 pbuf payload로 복사 */
    memcpy(p->payload, data, len);

    /* 송신 */
    err = udp_sendto(udp_send_pcb, p, dst_addr, dst_port);

    /* 송신 성공/실패와 무관하게 pbuf는 반드시 해제 */
    pbuf_free(p);

    return err;
}

err_t UdpSendBack(u16_t dst_port,
              const void *data, u16_t len)
{
    ip_addr_t *dst_addr = udp_addr;
    struct pbuf *p;
    err_t err;

    if (udp_send_pcb == NULL || dst_addr == NULL || data == NULL || len == 0) {
        return ERR_ARG;
    }

    /* PBUF_RAM: payload를 새 버퍼에 복사해 둠 (호출자가 data 버퍼를 즉시 재사용 가능) */
    p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL) {
        return ERR_MEM;
    }

    /* 사용자 데이터를 pbuf payload로 복사 */
    memcpy(p->payload, data, len);

    /* 송신 */
    err = udp_sendto(udp_send_pcb, p, dst_addr, dst_port);

    /* 송신 성공/실패와 무관하게 pbuf는 반드시 해제 */
    pbuf_free(p);

    return err;
}


/* 브로드캐스트 송신 (255.255.255.255) */
err_t UdpSendBroadcast(u16_t dst_port, const void *data, u16_t len)
{
    ip_addr_t bcast;
    /* lwIP 매크로: 255.255.255.255 */
    IP_ADDR4(&bcast, 255, 255, 255, 255);


    /* 브로드캐스트는 PCB에 SOF_BROADCAST 옵션이 있어야 송신 가능 */
    if (udp_send_pcb != NULL) {
        ip_set_option(udp_send_pcb, SOF_BROADCAST);
    }
    return UdpSend(&bcast, dst_port, data, len);
}

#endif /* LWIP_UDP */
