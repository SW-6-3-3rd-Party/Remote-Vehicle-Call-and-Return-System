#include "com.h"
#include "gettime.h"

/**
 * @brief DoIP 진단 라우팅 활성화 시 호출될 콜백 함수
 */
void Callback_Diag_RoutingActivated(void)
{
    /* 진단 라우팅이 활성화되면 원격 모드에서 진단 모드로 진입 */
    if (COM_Get_CurMode() == MODE_REMOTE)
    {
        COM_Set_CurMode(MODE_DIAG);
    }
}

/**
 * @brief DoIP 진단 세션 종료 시 호출될 콜백 함수
 */
void Callback_Diag_RoutingDeactivated(void)
{
    /* 진단이 종료되면 진단 모드에서 원격 모드로 이탈 */
    if (COM_Get_CurMode() == MODE_DIAG)
    {
        COM_Set_CurMode(MODE_REMOTE);
    }
}

void Callback_COM_SessionCnt(uint8_t *cnt) { cnt++;}

void Callback_COM_PcTimeout(void)
{
        COM_Set_Safety_Override(PC_TIMEOUT_BREAK);
        COM_Set_Turn_Signal(PC_TIMEOUT_LIGHT);
}

void SWC_BuzzerControlIndication(const uint8_t* msg)
{
    COM_Set_Collision_Warn(*msg);
}
