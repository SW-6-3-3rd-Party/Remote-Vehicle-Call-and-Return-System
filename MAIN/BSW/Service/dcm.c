#include <dcm.h>
#include "Platform_Types.h"
#include <stdint.h>
#include <string.h>

#define DTC_STATUS_TEST_FAILED      0x01
#define DTC_STATUS_CONFIRMED_DTC    0x08

/* 외부 송신 함수 선언 (PduR 또는 DoIP 계층의 Tx 래퍼) */
extern void DoIP_ProcessTx(uint16_t sourceAddr, uint8_t* udsData, uint16_t udsLength);


/* 현재 진단 세션 상태 (0x01: Default, 0x03: Extended) */
static uint8_t Current_Diag_Session = 0x01;


static uint8_t DtcStatus_Pc   = 0;
static uint8_t DtcStatus_Act  = 0;
static uint8_t DtcStatus_Body = 0;

static uint8_t DtcOccurCount_Pc   = 0;
static uint8_t DtcOccurCount_Act  = 0;
static uint8_t DtcOccurCount_Body = 0;

/**
 * @brief 통신 두절 시 COM 계층에서 호출하는 콜백
 */
void DCM_Callback_PcCommloss(void)
{
    DtcStatus_Pc |= DTC_STATUS_TEST_FAILED; /* 현재 통신 끊김 상태 */
    if(DtcOccurCount_Pc < 255)
        DtcOccurCount_Pc++;
    if(DtcOccurCount_Pc >= 3)
        DtcStatus_Pc |= DTC_STATUS_CONFIRMED_DTC;
}

void DCM_Callback_ActCommloss(void)
{
    DtcStatus_Act |= DTC_STATUS_TEST_FAILED; /* 현재 통신 끊김 상태 */
    if(DtcOccurCount_Act < 255)
        DtcOccurCount_Act++;
    if(DtcOccurCount_Act >= 3)
        DtcStatus_Act |= DTC_STATUS_CONFIRMED_DTC;
}


void DCM_Callback_BodyCommloss(void)
{
    DtcStatus_Body |= DTC_STATUS_TEST_FAILED; /* 현재 통신 끊김 상태 */
    if(DtcOccurCount_Body < 255)
        DtcOccurCount_Body++;
    if(DtcOccurCount_Body >= 3)
        DtcStatus_Body |= DTC_STATUS_CONFIRMED_DTC;
}

/**
 * @brief 통신이 다시 복구되었을 때 COM 수신부에서 호출
 */
void DCM_Callback_PcRecovered(void)
{
    DtcStatus_Pc &= ~DTC_STATUS_TEST_FAILED; /* 현재 통신은 다시 정상으로 돌아옴 */
    DtcOccurCount_Pc = 0;
}

void DCM_Callback_ActRecovered(void)
{
    DtcStatus_Act &= ~DTC_STATUS_TEST_FAILED; /* 현재 통신은 다시 정상으로 돌아옴 */
    DtcOccurCount_Act = 0;
}

void DCM_Callback_BodyRecovered(void)
{
    DtcStatus_Body &= ~DTC_STATUS_TEST_FAILED; /* 현재 통신은 다시 정상으로 돌아옴 */
    DtcOccurCount_Body = 0;
}

void DCM_Clear_All_DTC(void)
{
    DtcStatus_Act  = 0;
    DtcStatus_Body = 0;
    DtcStatus_Pc   = 0;

    DtcOccurCount_Pc = 0;
    DtcOccurCount_Act = 0;
    DtcOccurCount_Body = 0;
}

/* --- 1. UDS 0x22 (데이터 읽기) 용 인터페이스 --- */
uint8_t DCM_Get_ActEcu_CommStatus(void)  { return DtcStatus_Act & 0x01; }
uint8_t DCM_Get_BodyEcu_CommStatus(void) { return DtcStatus_Body & 0x01; }
uint8_t DCM_Get_Pc_CommStatus(void)      { return DtcStatus_Pc & 0x01; }

/* --- 2. UDS 0x19 (고장 코드 확인) 용 인터페이스 --- */
uint8_t DCM_Get_DtcStatus_ActEcu(void)  { return DtcStatus_Act; }
uint8_t DCM_Get_DtcStatus_BodyEcu(void) { return DtcStatus_Body; }
uint8_t DCM_Get_DtcStatus_Pc(void)      { return DtcStatus_Pc; }

/**
 * @brief UDS 네거티브 응답(NRC) 전송 헬퍼 함수
 */
static void Send_NegativeResponse(uint8_t reqSid, uint8_t nrc)
{
    uint8_t txBuf[3];
    txBuf[0] = 0x7F;     // NRC 고정 식별자
    txBuf[1] = reqSid;   // 거절된 요청 SID
    txBuf[2] = nrc;      // 에러 코드

    DoIP_ProcessTx(0x0001, txBuf, 3);
}

/**
 * @brief PduR에서 UDS 요청(Rx)이 올라왔을 때 호출되는 메인 처리 함수
 */
void Uds_ProcessRx(uint8_t* reqData, uint16_t reqLength)
{
    if (reqLength == 0) return;

    uint8_t sid = reqData[0];
    uint8_t txBuf[64]; // 지원하는 모든 DTC를 담아야 하므로 버퍼를 넉넉하게 잡습니다.
    uint16_t txLen = 0;

    switch (sid)
    {
        /* ========================================================== */
        /* 1. 진단 세션 제어 (SID: 0x10)                              */
        /* ========================================================== */
        case 0x10:
        {
            if (reqLength != 2) { Send_NegativeResponse(sid, 0x13); return; } // 길이 불일치

            uint8_t subFunc = reqData[1];
            if (subFunc == 0x01 || subFunc == 0x03)
            {
                Current_Diag_Session = subFunc;

                txBuf[0] = 0x50;     // Positive Response SID
                txBuf[1] = subFunc;  // Echo sub-function
                txLen = 2;
                DoIP_ProcessTx(0x0001, txBuf, txLen);
            }
            else
            {
                Send_NegativeResponse(sid, 0x12); // 지원하지 않는 서브펑션
            }
            break;
        }

        /* ========================================================== */
        /* 2. 데이터 읽기 (SID: 0x22)                                 */
        /* ========================================================== */
        case 0x22:
        {
            if (reqLength != 3) { Send_NegativeResponse(sid, 0x13); return; }

            uint16_t did = (reqData[1] << 8) | reqData[2];

            txBuf[0] = 0x62; // Positive Response SID
            txBuf[1] = (uint8_t)(did >> 8);
            txBuf[2] = (uint8_t)(did & 0xFF);
            txLen = 3;

            if (did == 0xF190) // ECU 식별 ("GW01")
            {
                txBuf[txLen++] = 'G';
                txBuf[txLen++] = 'W';
                txBuf[txLen++] = '0';
                txBuf[txLen++] = '1';
            }
            else if (did == 0x0400) // ACT ECU 통신 상태
            {
                txBuf[txLen++] = DCM_Get_ActEcu_CommStatus();
            }
            else if (did == 0x0401) // Body ECU 통신 상태
            {
                txBuf[txLen++] = DCM_Get_BodyEcu_CommStatus();
            }
            else if (did == 0x0402) // PC 통신 상태
            {
                txBuf[txLen++] = DCM_Get_Pc_CommStatus();
            }
            else
            {
                Send_NegativeResponse(sid, 0x31); // Request Out Of Range (지원 안 하는 DID)
                return;
            }

            DoIP_ProcessTx(0x0001, txBuf, txLen);
            break;
        }

        /* ========================================================== */
        /* 3. 고장 코드(DTC) 확인 (SID: 0x19)                         */
        /* ========================================================== */
        case 0x19:
        {
            if (reqLength != 2 && reqLength != 3 ) { Send_NegativeResponse(sid, 0x13); return; }
            // 0x0A는 마스크 파라미터가 없으므로 길이 2
            // 0x02는 마스크 파라미터가 있어서 길이 3

            uint8_t subFunc = reqData[1];

            /* 🚀 0x0A: Report Supported DTCs (지원하는 모든 DTC와 그 현재 상태 보고) */
            if (subFunc == 0x0A)
            {
                txBuf[0] = 0x59; // Positive Response SID
                txBuf[1] = 0x0A; // Echo sub-function
                txBuf[2] = 0xFF; // DTC Status Availability Mask (모든 상태 비트 지원)
                txLen = 3;

                uint8_t dtcStatus;

                // 1. ACT ECU 통신 두절 (0xC40000)
                dtcStatus = DCM_Get_DtcStatus_ActEcu(); // 발생: 0x09, 미발생(정상): 0x00
                txBuf[txLen++] = 0xC4; txBuf[txLen++] = 0x00; txBuf[txLen++] = 0x00;
                txBuf[txLen++] = dtcStatus;

                // 2. Body ECU 통신 두절 (0xC40100)
                dtcStatus = DCM_Get_DtcStatus_BodyEcu();
                txBuf[txLen++] = 0xC4; txBuf[txLen++] = 0x01; txBuf[txLen++] = 0x00;
                txBuf[txLen++] = dtcStatus;

                // 3. PC 통신 두절 (0xC40200)
                dtcStatus = DCM_Get_DtcStatus_Pc();
                txBuf[txLen++] = 0xC4; txBuf[txLen++] = 0x02; txBuf[txLen++] = 0x00;
                txBuf[txLen++] = dtcStatus;

                DoIP_ProcessTx(0x0001, txBuf, txLen);
            }
            else if (subFunc == 0x02)
            {
                uint8_t statusMask = reqData[2];

                txBuf[0] = 0x59; // Positive Response SID
                txBuf[1] = 0x02; // Echo sub-function
                txBuf[2] = 0xFF; // DTC Status Availability Mask
                txLen = 3;

                uint8_t dtcStatus;

                /* 1. ACT ECU 통신 두절 (0xC40000) */
                dtcStatus = DCM_Get_DtcStatus_ActEcu();

                if ((dtcStatus & statusMask) != 0)
                {
                    txBuf[txLen++] = 0xC4;
                    txBuf[txLen++] = 0x00;
                    txBuf[txLen++] = 0x00;
                    txBuf[txLen++] = dtcStatus;
                }

                /* 2. Body ECU 통신 두절 (0xC40100) */
                dtcStatus = DCM_Get_DtcStatus_BodyEcu();

                if ((dtcStatus & statusMask) != 0)
                {
                    txBuf[txLen++] = 0xC4;
                    txBuf[txLen++] = 0x01;
                    txBuf[txLen++] = 0x00;
                    txBuf[txLen++] = dtcStatus;
                }

                /* 3. PC 통신 두절 (0xC40200) */
                dtcStatus = DCM_Get_DtcStatus_Pc();

                if ((dtcStatus & statusMask) != 0)
                {
                    txBuf[txLen++] = 0xC4;
                    txBuf[txLen++] = 0x02;
                    txBuf[txLen++] = 0x00;
                    txBuf[txLen++] = dtcStatus;
                }

                DoIP_ProcessTx(0x0001, txBuf, txLen);
            }
            else
            {
                Send_NegativeResponse(sid, 0x12); // 지원하지 않는 서브펑션
            }
            break;
        }

        /* ========================================================== */
        /* 4. 고장 코드(DTC) 삭제 (SID: 0x14)                         */
        /* ========================================================== */
        case 0x14:
        {
            if (reqLength != 4) { Send_NegativeResponse(sid, 0x13); return; }

            uint32_t groupOfDTC = (reqData[1] << 16) | (reqData[2] << 8) | reqData[3];

            if (groupOfDTC == 0xFFFFFF) // 전체 삭제
            {
                // SWC에 구현된 RAM 변수(DTC) 초기화 함수 호출
                DCM_Clear_All_DTC();

                txBuf[0] = 0x54; // Positive Response SID
                txLen = 1;
                DoIP_ProcessTx(0x0001, txBuf, txLen);
            }
            else
            {
                Send_NegativeResponse(sid, 0x31); // 전체 삭제(0xFFFFFF) 외에는 지원 안 함
            }
            break;
        }

        /* ========================================================== */
        /* 지원하지 않는 서비스 (NRC 0x11 전송)                       */
        /* ========================================================== */
        default:
        {
            Send_NegativeResponse(sid, 0x11); // Service Not Supported
            break;
        }
    }
}


