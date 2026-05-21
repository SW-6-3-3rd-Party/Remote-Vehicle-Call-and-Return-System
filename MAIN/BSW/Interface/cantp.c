#include "cantp.h"
#include "canif.h"
#include "pdur.h"
#include "pdur_cfg.h"

void CanTp_RxIndication(uint32_t canId, uint8_t* payload, uint8_t length)
{
    /* 방어 코드: CAN 프레임은 최소 1바이트(PCI) 이상이어야 함 */
    if (length < 1) return;

    /* 1. PCI(Protocol Control Information) 바이트 해석 */
    uint8_t pciByte = payload[0];
    uint8_t frameType = (pciByte & 0xF0) >> 4; // 상위 4비트 추출
    uint8_t udsLength = (pciByte & 0x0F);      // 하위 4비트 추출 (데이터 길이)

    /* 2. Single Frame(0x0)인지 확인 */
    if (frameType == 0x00)
    {
        /* 방어 코드: PCI에 적힌 길이가 실제 수신된 데이터보다 큰지 확인 */
        if (udsLength > (length - 1)) return;

        /* 3. 헤더(1바이트)를 떼어내고 알맹이 위치 계산 */
        uint8_t* udsData = payload + 1;

        /* 4. CAN ID를 기반으로 목적지(Body vs Actuator) 판별 후 PduR로 전달 */
        if (canId == 0x708) // 예: Body ECU의 UDS 응답 ID
        {
            PduR_RouteRx(PDUR_CAN_ACT_UDS_ID, udsData, udsLength);
        }
        else if (canId == 0x718) // 예: Actuator ECU의 UDS 응답 ID
        {
            PduR_RouteRx(PDUR_CAN_BODY_UDS_ID, udsData, udsLength);
        }
        else
        {
            // 등록되지 않은 진단 응답은 무시
        }
    }
    else
    {
        /* (추후 확장) 다중 프레임(First/Consecutive Frame) 처리 로직 */
    }
}

/**
 * @brief PduR에서 UDS 데이터를 받아 CAN Single Frame으로 조립 후 송신
 * @param canId     목적지 하위 ECU의 CAN ID (물리적 주소, 예: 0x700, 0x710)
 * @param udsData   DoIP -> PduR을 거쳐 내려온 순수 UDS 요청 데이터
 * @param udsLength UDS 데이터 길이
 */
void CanTp_Transmit(uint32_t canId, uint8_t* udsData, uint16_t udsLength)
{
    /* 1. 방어 코드: Single Frame은 UDS 데이터가 최대 7바이트까지만 가능 */
    if (udsLength == 0 || udsLength > 7)
    {
        return; /* 다중 프레임 전송 불가 (현재 미구현) */
    }

    uint8_t canPayload[8];

    /* 2. PCI 바이트 생성 (Single Frame = 0x00 + 길이) */
    canPayload[0] = (uint8_t)(0x00 | (udsLength & 0x0F));

    /* 3. UDS 알맹이 복사 */
    for (uint16_t i = 0; i < udsLength; i++)
    {
        canPayload[1 + i] = udsData[i];
    }

    /* 4. 남는 공간 패딩 (Padding) 처리 */
    // 프레임을 항상 8바이트로 채워서 보내는 것이 네트워크 안정성에 좋습니다.
    for (uint16_t i = 1 + udsLength; i < 8; i++)
    {
        canPayload[i] = 0;
    }

    /* 5. 완성된 8바이트 프레임을 CanIf로 전달 */
    CanIf_Transmit(canId, canPayload, 8);
}
