#include "doip.h"
#include "soad.h"
#include "pdur.h"
#include "pdur_cfg.h"
#include "swc.h"
#include "Platform_Types.h"

#include <string.h>

#define TESTER_SOURCE_ADDRESS 0x0E00
#define MY_TARGET_ADDRESS 0x0001


/**
 * @brief 제네릭 헤더 NACK 전송 (Payload Type: 0x0000)
 */
void DoIP_SendGenericNack(uint16_t port, uint8_t nackCode)
{
    uint8_t txBuf[9]; // 헤더(8) + NACK코드(1) = 9바이트

    txBuf[0] = 0x02; // Protocol Version
    txBuf[1] = 0xFD; // Inverse Version
    txBuf[2] = 0x00; // Payload Type: 0x0000 (Generic DoIP header NACK)
    txBuf[3] = 0x00;

    txBuf[4] = 0x00; // Length: 1 바이트
    txBuf[5] = 0x00;
    txBuf[6] = 0x00;
    txBuf[7] = 0x01;

    txBuf[8] = nackCode; // 에러 원인 코드 (0x00, 0x01, 0x04 등)

    SoAd_IfTransmit(port, txBuf, 9);
}

/**
 * @brief 진단 메시지 라우팅 긍정 응답 (ACK) 전송 (Payload Type: 0x8002)
 */
void DoIP_SendDiagAck(uint16_t port, uint16_t sa, uint16_t ta, uint8_t ackCode)
{
    uint8_t txBuf[13]; // 헤더(8) + SA(2) + TA(2) + ACK코드(1) = 13바이트

    txBuf[0] = 0x02;
    txBuf[1] = 0xFD;
    txBuf[2] = 0x80; // Payload Type: 0x8002 (Diagnostic message positive ACK)
    txBuf[3] = 0x02;

    txBuf[4] = 0x00; // Length: 5 바이트
    txBuf[5] = 0x00;
    txBuf[6] = 0x00;
    txBuf[7] = 0x05;

    txBuf[8]  = (uint8_t)(sa >> 8);   // Source Address (빅엔디안)
    txBuf[9]  = (uint8_t)(sa & 0xFF);
    txBuf[10] = (uint8_t)(ta >> 8);   // Target Address (빅엔디안)
    txBuf[11] = (uint8_t)(ta & 0xFF);
    txBuf[12] = ackCode;              // 성공 코드 (보통 0x00)

    SoAd_IfTransmit(port, txBuf, 13);
}

/**
 * @brief 진단 메시지 라우팅 부정 응답 (NACK) 전송 (Payload Type: 0x8003)
 */
void DoIP_SendDiagNack(uint16_t port, uint16_t sa, uint16_t ta, uint8_t nackCode)
{
    uint8_t txBuf[13]; // 구조는 ACK와 동일하나 Payload Type만 다름

    txBuf[0] = 0x02;
    txBuf[1] = 0xFD;
    txBuf[2] = 0x80; // Payload Type: 0x8003 (Diagnostic message negative ACK)
    txBuf[3] = 0x03;

    txBuf[4] = 0x00; // Length: 5 바이트
    txBuf[5] = 0x00;
    txBuf[6] = 0x00;
    txBuf[7] = 0x05;

    txBuf[8]  = (uint8_t)(sa >> 8);
    txBuf[9]  = (uint8_t)(sa & 0xFF);
    txBuf[10] = (uint8_t)(ta >> 8);
    txBuf[11] = (uint8_t)(ta & 0xFF);
    txBuf[12] = nackCode; // 에러 코드 (0x02, 0x03 등)

    SoAd_IfTransmit(port, txBuf, 13);
}

/**
 * @brief 라우팅 활성화(Routing Activation)를 통해 승인된 Source Address인지 검사
 */
uint8_t DoIP_IsRegisteredSA(uint16_t port, uint16_t sa)
{
    // (원래는 라우팅 활성화 단계에서 저장해둔 현재 소켓의 SA 변수와 비교해야 함)
    // 현재는 테스터 주소(0x0030)와 일치하는지만 단순 검사
    if (sa == TESTER_SOURCE_ADDRESS)
    {
        return 1; // TRUE (승인됨)
    }
    return 0; // FALSE (미승인, 즉시 소켓 종료 대상)
}

/**
 * @brief 현재 게이트웨이가 알고 있는 하위 ECU의 Target Address인지 검사
 */
uint8_t DoIP_IsKnownTA(uint16_t ta)
{
    // 프로젝트 사양에 등록된 ECU들의 주소 목록
    if (ta == 0x0001 || ta == 0x0010 || ta == 0x0020)
    {
        return 1; // TRUE (알고 있는 주소)
    }
    return 0; // FALSE (모르는 주소, 라우팅 불가)
}

/**
 * @brief 라우팅 활성화 응답 메시지(0x0006) 송신 헬퍼
 */
void DoIP_SendRoutingActivationResponse(uint16_t port, uint16_t testerSA, uint8_t responseCode)
{
    uint8_t txBuf[17]; // 헤더(8) + 응답 페이로드(9) = 17바이트

    txBuf[0] = 0x02; txBuf[1] = 0xFD; // 버전
    txBuf[2] = 0x00; txBuf[3] = 0x06; // Payload Type: 0x0006

    // 길이: 9바이트 (테스터SA(2) + 제어기SA(2) + 코드(1) + 예약(4))
    txBuf[4] = 0x00; txBuf[5] = 0x00; txBuf[6] = 0x00; txBuf[7] = 0x09;

    txBuf[8]  = (uint8_t)(testerSA >> 8);   // 테스터 SA (Echo back)
    txBuf[9]  = (uint8_t)(testerSA & 0xFF);
    txBuf[10] = (uint8_t)(MY_TARGET_ADDRESS >> 8); // 제어기 SA
    txBuf[11] = (uint8_t)(MY_TARGET_ADDRESS & 0xFF);
    txBuf[12] = responseCode;               // 결과 코드

    /* 예약된 4바이트(13~16)는 0으로 채움 */
    memset(&txBuf[13], 0x00, 4);

    SoAd_IfTransmit(port, txBuf, 17);
}


/**
 * @brief 라우팅 활성화 요청(0x0005)을 처리하고 응답(0x0006)을 송신
 */
void DoIP_ProcessRoutingActivation(uint16_t port, uint8_t* payload, uint16_t length)
{
    // 1. 최소 길이 검증 (헤더 8 + 페이로드 7 = 15바이트)
    if (length < 15) return;

    // 2. 테스터의 Source Address 추출 (페이로드 시작점 + 헤더 8바이트 = 8번지부터)
    uint16_t testerSA = (payload[8] << 8) | payload[9];
    uint8_t activationType = payload[10];

    uint8_t responseCode = 0x00; // 초기값: 실패/거부

    // 3. 검증 로직: 단순 SA 확인
    if (testerSA == TESTER_SOURCE_ADDRESS)
    {
        // 성공: 응답 코드 0x10 (Success)
        responseCode = 0x10;

        // [중요] 이 소켓이 이제 "라우팅 활성화됨" 상태임을 내부적으로 기억
        // 하지말고 그냥 콜백으로 진단 상태임을 알리자. 어차피 진단 상태면 얘잖아
//        DoIP_RegisterActiveSocket(port, testerSA);
        Callback_Diag_RoutingActivated();
    }
    else
    {
        // 실패: 응답 코드 0x00 (Unknown SA)
        responseCode = 0x00;
    }

    // 4. 라우팅 활성화 응답(0x0006) 조립 및 송신
    DoIP_SendRoutingActivationResponse(port, testerSA, responseCode);

    // 5. 만약 실패했다면 규격에 따라 소켓 종료
    if (responseCode != 0x10)
    {
        SoAd_CloseSocket(port);
    }
}




/**
 * @brief ISO 13400 규격에 따른 DoIP 수신, 헤더 검증, ACK/NACK 처리 함수
 */
void DoIP_ProcessRx(uint8_t* payload, uint16_t length)
{
    /* ===================================================================== */
    /* 1단계: 제네릭 헤더 검증 (Generic Header Check)                        */
    /* ===================================================================== */

    uint16_t port = 13400;

    // 1-0. 최소 헤더 길이 검증
    if (length < 8) return;

    // 1-1. 동기화 패턴 (프로토콜 버전) 검증
//    if (payload[0] != 0x02 || payload[1] != 0xFD)
//    {
//        DoIP_SendGenericNack(port, 0x00); // Incorrect pattern format
//        SoAd_CloseSocket(port);           // [소켓 강제 종료]
//        return;
//    }

    uint16_t payloadType = (payload[2] << 8) | payload[3];
    uint32_t expectedPayloadLength = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];

    // 1-2. 페이로드 길이 일치 여부 검증
    if (expectedPayloadLength != (length - 8))
    {
        DoIP_SendGenericNack(port, 0x04); // Invalid payload length
        SoAd_CloseSocket(port);           // [소켓 강제 종료]
        return;
    }

    /* ===================================================================== */
    /* 2단계: 페이로드 타입별 처리 및 라우팅 검증                            */
    /* ===================================================================== */

    if (payloadType == 0x8001) /* 진단 메시지 (Diagnostic Message) */
    {
        // 진단 메시지는 헤더(8) + SA(2) + TA(2) = 최소 12바이트 필요
        if (length < 12) return;

        uint16_t sourceAddr = (payload[8] << 8) | payload[9];
        uint16_t targetAddr = (payload[10] << 8) | payload[11];

        // 2-A. 소스 주소(SA) 검증: 현재 소켓이 라우팅 활성화 단계에서 승인받은 SA와 일치하는가?
        if (DoIP_IsRegisteredSA(port, sourceAddr) == FALSE)
        {
            DoIP_SendDiagNack(port, targetAddr, sourceAddr, 0x02); // Invalid source address
            SoAd_CloseSocket(port);                                // [소켓 강제 종료]
            return;
        }

        // 2-B. 타겟 주소(TA) 검증: 우리가 아는 하위 네트워크(CAN)의 ECU 주소인가?
        if (DoIP_IsKnownTA(targetAddr) == FALSE)
        {
            DoIP_SendDiagNack(port, targetAddr, sourceAddr, 0x03); // Unknown target address
            return;
        }

        /* ----------------------------------------------------------------- */
        /* 라우팅 검증 성공! Positive ACK 전송 및 알맹이 추출하여 PduR 전달  */
        /* ----------------------------------------------------------------- */
        DoIP_SendDiagAck(port, targetAddr, sourceAddr, 0x00); // Routing confirmation ACK

        uint8_t* udsData = payload + 12;
        uint16_t udsLength = length - 12;

        /* TA에 따라 타겟 네트워크 PduR ID 분기 (이전 CanTp 로직과 동일한 원리) */
        if (targetAddr == 0x0001)
            PduR_RouteRx(PDUR_DOIP_MAIN_TCP_ID, udsData, udsLength);
        else if (targetAddr == 0x0010)
            PduR_RouteRx(PDUR_DOIP_ACT_TCP_ID, udsData, udsLength);
        else if (targetAddr == 0x0020)
            PduR_RouteRx(PDUR_DOIP_BODY_TCP_ID, udsData, udsLength);
    }
    else if (payloadType == 0x0005) /* 라우팅 활성화 요청 (Routing Activation) */
    {
         DoIP_ProcessRoutingActivation(port, payload, length);
    }
    else
    {
        // 1-3. 지원하지 않는 페이로드 타입
        DoIP_SendGenericNack(port, 0x01); // Unknown payload type
    }
}

/**
 * @brief 하위 제어기(CAN)에서 올라온 UDS 응답 데이터를 DoIP 헤더(0x8001)로 포장하여 송신
 * @param sourceAddr 응답을 보낸 제어기의 논리 주소 (예: 0x0001, 0x0010)
 * @param udsData    CanTp -> PduR을 거쳐 올라온 순수 UDS 응답 데이터
 * @param udsLength  UDS 데이터 길이
 */
void DoIP_ProcessTx(uint16_t sourceAddr, uint8_t* udsData, uint16_t udsLength)
{
    /* 임시 송신 버퍼 (최대 길이는 프로젝트 사양에 맞게 조절, 보통 UDS는 4K까지 가능하지만 예시로 1024 사용) */
    uint8_t txBuf[1024];

    // DoIP 페이로드 길이 = SA(2바이트) + TA(2바이트) + UDS 알맹이 길이
    uint32_t doipPayloadLength = 4 + udsLength;
    uint16_t totalTcpLength = 8 + doipPayloadLength; // 헤더 8바이트 포함 총 길이

    if (totalTcpLength > sizeof(txBuf)) return; // 버퍼 오버플로우 방어

    /* 1. 제네릭 헤더 작성 */
    txBuf[0] = 0x02; // Protocol Version
    txBuf[1] = 0xFD; // Inverse Version
    txBuf[2] = 0x80; // Payload Type: 0x8001 (Diagnostic Message)
    txBuf[3] = 0x01;

    txBuf[4] = (uint8_t)(doipPayloadLength >> 24);
    txBuf[5] = (uint8_t)(doipPayloadLength >> 16);
    txBuf[6] = (uint8_t)(doipPayloadLength >> 8);
    txBuf[7] = (uint8_t)(doipPayloadLength & 0xFF);

    /* 2. Source Address 및 Target Address 작성 */
    // 송신 시에는 SA가 "응답을 보내는 ECU 주소"가 되고, TA가 "테스터 주소(0x0030)"가 됩니다. (Rx와 반대)
    txBuf[8]  = (uint8_t)(sourceAddr >> 8);
    txBuf[9]  = (uint8_t)(sourceAddr & 0xFF);
    txBuf[10] = (uint8_t)(TESTER_SOURCE_ADDRESS >> 8);
    txBuf[11] = (uint8_t)(TESTER_SOURCE_ADDRESS & 0xFF);

    /* 3. UDS 알맹이 복사 */
    memcpy(&txBuf[12], udsData, udsLength);

    /* 4. 완성된 패킷을 SoAd로 전달하여 TCP 송신 (포트는 13400 고정) */
    SoAd_IfTransmit(13400, txBuf, totalTcpLength);
}
