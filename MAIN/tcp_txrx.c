#include "tcp_txrx.h"
#include "lwip/opt.h"

#if LWIP_TCP
#include "lwip/tcp.h"
#include <string.h>
#include "soad.h" /* SoAd 계층 연동을 위한 헤더 포함 */

#define TCP_SERVER_PORT 13400

/* 제어 블록 (단일 연결 가정) */
static struct tcp_pcb *tcp_listen_pcb = NULL;
static struct tcp_pcb *tcp_active_pcb = NULL;

/* 콜백 함수 원형 */
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  tcp_server_error(void *arg, err_t err);
static void  tcp_server_close(struct tcp_pcb *tpcb);

/* ------------------------------------------------------------------ */
/* 1. 초기화                                                          */
/* ------------------------------------------------------------------ */
void TcpInit(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb == NULL) return;

    if (tcp_bind(pcb, IP_ANY_TYPE, TCP_SERVER_PORT) != ERR_OK) {
        tcp_close(pcb);
        return;
    }
    tcp_listen_pcb = tcp_listen_with_backlog(pcb, 1);
    if (tcp_listen_pcb) {
        tcp_accept(tcp_listen_pcb, tcp_server_accept);
    }
}

/* ------------------------------------------------------------------ */
/* 2. 연결 수락                                                       */
/* ------------------------------------------------------------------ */
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    LWIP_UNUSED_ARG(arg);
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

    /* 이미 연결된 대상이 있으면 새 연결 거절 */
    if (tcp_active_pcb != NULL) return ERR_ABRT;

    tcp_active_pcb = newpcb;

    /* 필수 콜백만 등록 (sent, poll 콜백 생략 가능) */
    tcp_recv(newpcb, tcp_server_recv);
    tcp_err(newpcb, tcp_server_error);

    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* 3. 데이터 수신 (SoAd 계층으로 전달)                                */
/* ------------------------------------------------------------------ */
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    LWIP_UNUSED_ARG(arg);

    /* 상대방이 연결을 끊음 (FIN) */
    if (p == NULL) {
        tcp_server_close(tpcb);
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    /* 1. 수신 윈도우 갱신 (lwIP에 데이터 잘 받았다고 알림) */
    tcp_recved(tpcb, p->tot_len);

    /* 2. 수신된 데이터를 SoAd 계층으로 전달 */
    /* tpcb->local_port를 통해 수신 포트(13400)를 전달하여 SoAd에서 라우팅 처리 가능하도록 함 */
    SoAd_RxIndication(tpcb->local_port, (uint8_t*)p->payload, p->len);

    /* 3. 다 쓴 수신 버퍼 즉시 해제 */
    pbuf_free(p);

    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* 4. 에러 처리                                                       */
/* ------------------------------------------------------------------ */
static void tcp_server_error(void *arg, err_t err) {
    LWIP_UNUSED_ARG(arg);
    //LWIP_UNUSED_ 임의(err);
    tcp_active_pcb = NULL; /* lwIP가 pcb를 알아서 해제함 */
}

/* ------------------------------------------------------------------ */
/* 5. 연결 종료                                                       */
/* ------------------------------------------------------------------ */
static void tcp_server_close(struct tcp_pcb *tpcb) {
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_close(tpcb);
    if (tcp_active_pcb == tpcb) tcp_active_pcb = NULL;
}

/* ------------------------------------------------------------------ */
/* 6. 외부에서 원할 때 쏘는 송신 함수                                 */
/* ------------------------------------------------------------------ */
err_t TcpSend(const void *data, u16_t len) {
    if (tcp_active_pcb == NULL) return ERR_CONN;
    if (data == NULL || len == 0) return ERR_ARG;
    if (len > tcp_sndbuf(tcp_active_pcb)) return ERR_MEM;

    err_t err = tcp_write(tcp_active_pcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(tcp_active_pcb);
    }
    return err;
}

void doip_server_close(void)
{
    tcp_server_close(tcp_active_pcb);
}

#endif /* LWIP_TCP */
