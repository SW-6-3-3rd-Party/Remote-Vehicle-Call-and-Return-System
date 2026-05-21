#include "canif_cfg.h"
#include "Platform_Types.h"
#include <stddef.h>

/* CanDrv(하드웨어 드라이버)가 수신 직후 호출해 주는 함수 */
void CanIf_RxIndication(uint32_t canId, uint8_t* payload, uint8_t length)
{
    /* 테이블을 돌며 매칭되는 CAN ID를 찾음 */
    for (uint16_t i = 0; i < CANIF_RX_TABLE_SIZE; i++)
    {
        if (CanIf_RxTable[i].canId == canId)
        {
            /* 매칭되는 CAN ID를 찾으면 상위 모듈로 쏴줌 */
            if (CanIf_RxTable[i].callback != NULL)
            {
                CanIf_RxTable[i].callback(payload, length);
            }
            return;
        }
    }

    // 등록되지 않은 CAN ID는 무시 (필터링 효과)
}

void CanIf_Transmit(uint32_t canId, uint8_t* payload, uint8_t length)
{
    /* 1. 방어 코드: Classic CAN 규격상 페이로드는 최대 8바이트 */
    if (length > 8)
    {
        return; // 프로젝트에 따라 에러 로깅(Det_ReportError) 추가 가능
    }

    /* 2. 하드웨어 드라이버로 송신 지시 */
    boolean isSent = Can_Send(canId, payload, length);

    /* 3. 송신 결과 처리는 필요 시 구현 */
}
