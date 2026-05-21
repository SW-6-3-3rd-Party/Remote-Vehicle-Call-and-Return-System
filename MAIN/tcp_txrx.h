#ifndef TCP_TXRX_H
#define TCP_TXRX_H

#include "lwip/err.h"
#include "lwip/ip_addr.h"

/* TCP 서버 초기화 (지정 포트로 LISTEN) */
void TcpInit(void);
void doip_server_close(void);
/* 현재 연결된 클라이언트로 송신
 * - 연결이 없으면 ERR_CONN 반환
 * - 송신 버퍼가 부족하면 ERR_MEM 반환 (재시도 필요)
 */
err_t TcpSend(const void *data, u16_t len);

#endif /* TCP_TXRX_H */
