#include "com.h"
#include "gettime.h"



/* 타임아웃 임계값 설정 */
#define UDP_TIMEOUT_MS  500   /* UDP 제어 신호가 500ms 동안 없으면 기본 모드로 복귀 */
#define E2E_TIMEOUT_MS  600   /* 바디 카운터가 600ms 동안 변하지 않으면 이상으로 판단 */

/* SWC 내부 정적 변수 (상태 모니터링용) */
static uint32_t Last_Udp_RxTime = 0;   /* 마지막 UDP 수신 시점 (ms) */
static uint32_t Last_E2e_ChangeTime = 0; /* 바디 카운터가 마지막으로 변경된 시점 (ms) */
static uint8_t  Last_Body_AliveCnt = 0;  /* 직전 주기의 바디 카운터 값 */
static boolean  Is_Body_Anomalous = FALSE; /* 바디 상태 이상 플래그 */


/* 실시간 통신 상태 (0: 정상, 1: 두절) -> 통신 복구 시 0으로 돌아감 */
static uint8_t CommStatus_Act  = 0;
static uint8_t CommStatus_Body = 0;
static uint8_t CommStatus_Pc   = 0;

/* 고장 코드 발생 기록 (0: 미발생, 1: 발생함) -> UDS 0x14로만 초기화됨 */
static uint8_t DtcRecord_Act  = 0;
static uint8_t DtcRecord_Body = 0;
static uint8_t DtcRecord_Pc   = 0;

/**
 * @brief 통신 두절 시 COM 계층에서 호출하는 콜백
 */
void SWC_Callback_PcCommloss(void)
{
    CommStatus_Pc = 1; /* 현재 통신 끊김 상태! */
    DtcRecord_Pc  = 1; /* 고장 이력에 '발생'으로 도장 쾅 찍음 (Latching) */
}

void SWC_Callback_ActCommloss(void)
{
    CommStatus_Act = 1; /* 현재 통신 끊김 상태! */
    DtcRecord_Act  = 1; /* 고장 이력에 '발생'으로 도장 쾅 찍음 (Latching) */
}


void SWC_Callback_BodyCommloss(void)
{
    CommStatus_Body = 1; /* 현재 통신 끊김 상태! */
    DtcRecord_Body  = 1; /* 고장 이력에 '발생'으로 도장 쾅 찍음 (Latching) */
}

/**
 * @brief 통신이 다시 복구되었을 때 COM 수신부에서 호출
 */
void SWC_Callback_PcRecovered(void)
{
    CommStatus_Pc = 0; /* 현재 통신은 다시 정상으로 돌아옴 */
}

void SWC_Callback_ActRecovered(void)
{
    CommStatus_Act = 0; /* 현재 통신은 다시 정상으로 돌아옴 */
}

void SWC_Callback_BodyRecovered(void)
{
    CommStatus_Body = 0; /* 현재 통신은 다시 정상으로 돌아옴 */
}

void SWC_Clear_All_DTC(void)
{
    DtcRecord_Act  = 0;
    DtcRecord_Body = 0;
    DtcRecord_Pc   = 0;

    /* 통신이 아직도 끊겨있는 상태인데 강제로 지우면 안 되므로,
       CommStatus가 0(정상)일 때만 지워지도록 방어 로직을 넣어도 됨.
    */
}

/* --- 1. UDS 0x22 (데이터 읽기) 용 인터페이스 --- */
uint8_t SWC_Get_ActEcu_CommStatus(void)  { return CommStatus_Act; }
uint8_t SWC_Get_BodyEcu_CommStatus(void) { return CommStatus_Body; }
uint8_t SWC_Get_Pc_CommStatus(void)      { return CommStatus_Pc; }

/* --- 2. UDS 0x19 (고장 코드 확인) 용 인터페이스 --- */
uint8_t SWC_Get_DtcStatus_ActEcu(void)  { return DtcRecord_Act; }
uint8_t SWC_Get_DtcStatus_BodyEcu(void) { return DtcRecord_Body; }
uint8_t SWC_Get_DtcStatus_Pc(void)      { return DtcRecord_Pc; }


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
        Last_Udp_RxTime = Get_SystemTime_ms(); /* 이탈 직후 즉시 타임아웃되는 것을 방지하기 위해 타이머 리셋 */
    }
}

void Callback_COM_SessionCnt(uint8_t *cnt) { cnt++;}

void Callback_COM_PcTimeout(void)
{
    if(COM_Get_Ignition() == IGNITIONON)
    {
        COM_Set_Safety_Override(PC_TIMEOUT_BREAK);
        COM_Set_Turn_Signal(PC_TIMEOUT_LIGHT);
    }
    COM_Set_CurMode(MODE_DEFAULT);
}
/**
 * @brief 메인 루프에서 주기적으로 실행될 SWC 통합 제어 및 Fail-Safe 함수
 */
//void SWC_Control_MainFunction(void)
//{
//    uint32_t currentTime = Get_SystemTime_ms();
//    uint8_t currentMode = COM_Get_CurMode();
//
//    /* ===================================================================== */
//    /* 과제 1: 바디 Alive Counter 검사 (E2E 변함 없는 이상 감시)              */
//    /* ===================================================================== */
//    uint8_t currentBodyAliveCnt = COM_Get_BodyAliveCnt();
//
//    if (currentBodyAliveCnt != Last_Body_AliveCnt)
//    {
//        /* 카운터가 정상적으로 변함 -> 시간 갱신 및 정상 판단 */
//        Last_E2e_ChangeTime = currentTime;
//        Last_Body_AliveCnt = currentBodyAliveCnt;
//        Is_Body_Anomalous = FALSE;
//    }
//    else
//    {
//        /* 카운터가 멈춤 -> 설정된 시간(600ms) 동안 안 변하면 이상 판정 */
//        if ((currentTime - Last_E2e_ChangeTime) >= E2E_TIMEOUT_MS)
//        {
//            Is_Body_Anomalous = TRUE; /* 바디 제어기 통신 불능 상태 */
//        }
//    }
//
//    /* ===================================================================== */
//    /* 과제 2: Main 타임아웃 동작                                                 */
//    /* ===================================================================== */
//
//
//    /* ===================================================================== */
//    /* 과제 3: 최종 제어 명령 및 Fail-Safe 출력 결정                           */
//    /* ===================================================================== */
//
//    /* 바디가 고장 났거나(Is_Body_Anomalous)인 경우 Safe State 발동 */
//    if (COM_Get_CurMode() == MODE_DEFAULT)
//    {
//        /* 안전 상태 (Safe State) 값 적용 */
//        // 예: 가속 0, 브레이크 100%, 기어 중립 등 하위 CAN으로 보낼 버퍼를 안전값으로 강제 오버라이드
//    }
//    else if (COM_Get_CurMode() == MODE_REMOTE)
//    {
//
//    }
//    else if (COM_Get_CurMode() == MODE_DIAG)
//    {
//
//    }
//}
