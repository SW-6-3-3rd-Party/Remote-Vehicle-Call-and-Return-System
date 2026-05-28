/*
 *  유독 하드코딩이 많은 곳이므로 수정에 주의
 */


/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#include "com.h"
#include "pdur.h"
#include "pdur_cfg.h"
#include "swc.h"
#include "gettime.h"
#include "dcm.h"
#include "Platform_Types.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
#define MAX_PAYLOAD 256

#define UDP_TIMEOUT_THRESHOLD_MS 500  /* 500ms 통신 두절 시 타임아웃 */
#define ACT_TIMEOUT_THRESHOLD_MS 500
#define BODY_TIMEOUT_THRESHOLD_MS 500

#define SOMEIP_SERVICE_ID    0x2001
#define SOMEIP_METHOD_ID     0x0001

/*********************************************************************************************************************/
/*-------------------------------------------------Global variables--------------------------------------------------*/
/*********************************************************************************************************************/

/*********************************************************************************************************************/
/*--------------------------------------------Private Variables/Constants--------------------------------------------*/
/*********************************************************************************************************************/

//제어 필드
typedef struct
{
        boolean accel;
        boolean brake;
        uint8_t steer;
        uint8_t gear;
        uint8_t turn_signal; //방향 지시등(비상등도)
        boolean horn;        //경적
        boolean ignition;    //시동
        boolean head_light;  //전조등
}COMRxUDPCtrType;

typedef struct
{
    uint32_t messageId;
    uint32_t requestId;

    uint8_t protocolVersion;
    uint8_t interfaceVersion;
    uint8_t messageType;
    uint8_t returnCode;

    uint32_t payloadLength;
    uint8_t payload[MAX_PAYLOAD];
} COMRxSomeIPType;

typedef struct
{
        uint8_t speedl;
        uint8_t speedh;
        uint8_t gear;
        uint8_t steer;
        uint8_t steering_angle;
}COMRxCANStatType;

typedef struct
{
        uint8_t alive_cnt;
}COMRxCANBodyType;

static COMRxUDPCtrType COM_RxBuf_UDP_Ctr;
static COMRxCANStatType COM_RxBuf_CAN_Stat;
static COMRxCANBodyType COM_RxBuf_CAN_Body;

static boolean COM_Collision_Warn = 1;

static uint8_t COM_Cur_Mode;
static boolean COM_Safety_Override;
static uint8_t COM_Stat_Cnt;

static uint8_t COM_TxBuf_CtrAct_CAN[6];
static uint8_t COM_TxBuf_CtrBody_CAN[6];
static uint8_t COM_TxBuf_Stat_UDP[3];





static uint32_t Last_Udp_RxTime = 0;
static uint32_t Last_Act_RxTime = 0;
static uint32_t Last_Body_RxTime = 0;

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
static void SomeIp_SendResponse(uint8_t* rxPacket, uint8_t buzzerState);
/*********************************************************************************************************************/
/*---------------------------------------------Function Implementations----------------------------------------------*/
/*********************************************************************************************************************/
void UDP_Ctr_ProcessRx(uint8_t* payload, uint16_t length)
{
    COM_RxBuf_UDP_Ctr.accel     =   payload[1];
    COM_RxBuf_UDP_Ctr.brake     =   payload[2];
    COM_RxBuf_UDP_Ctr.steer     =   payload[3];
    COM_RxBuf_UDP_Ctr.gear      =   payload[4];
    COM_RxBuf_UDP_Ctr.turn_signal = payload[5];
    COM_RxBuf_UDP_Ctr.horn      =   payload[6];
    COM_RxBuf_UDP_Ctr.ignition  =   payload[7];
    COM_RxBuf_UDP_Ctr.head_light =  payload[8];

    if(COM_RxBuf_UDP_Ctr.ignition == IGNITIONON ) COM_Cur_Mode = MODE_REMOTE;
    else if(COM_RxBuf_UDP_Ctr.ignition == IGNITIONOFF ) COM_Cur_Mode = MODE_DEFAULT;
    /* 핵심: 정상 수신되었으므로 시간 갱신 및 타임아웃 플래그 해제 */
    Last_Udp_RxTime = Get_SystemTime_ms();
    COM_Set_Safety_Override(PC_DEFAULT);
    DCM_Callback_PcRecovered();
}

void SomeIp_ProcessRx(uint8_t* payload, uint16_t length)
{
    if (length < 18)
            return;

    /* =========================
     * SOME/IP Header Parsing
     * ========================= */

    uint16_t serviceId =
        ((uint16_t)payload[0] << 8) |
         payload[1];

    uint16_t methodId =
        ((uint16_t)payload[2] << 8) |
         payload[3];

    if (serviceId != SOMEIP_SERVICE_ID)
        return;

    if (methodId != SOMEIP_METHOD_ID)
        return;

    /* =========================
     * Payload Deserialize
     * ========================= */

    Com_BuzzerControlType msg;

    msg.vehicle_id  = payload[16];
    msg.buzzer_state = payload[17];

    /* =========================
     * SWC 처리
     * ========================= */

    SWC_BuzzerControlIndication(&msg.buzzer_state);

    /* =========================
     * Response 송신
     * ========================= */

    SomeIp_SendResponse(
        payload,
        COM_Collision_Warn
    );
}

void CAN_Stat_processRx(uint8_t* payload, uint16_t length)
{
    COM_RxBuf_CAN_Stat.speedl         =   payload[0];
    COM_RxBuf_CAN_Stat.speedh         =   payload[1];
    COM_RxBuf_CAN_Stat.gear           =   payload[2];
    COM_RxBuf_CAN_Stat.steer          =   payload[3];
    COM_RxBuf_CAN_Stat.steering_angle =   payload[4];
    Last_Act_RxTime = Get_SystemTime_ms();
    DCM_Callback_ActRecovered();
}

void CAN_Body_processRx(uint8_t* payload, uint16_t length)
{
    COM_RxBuf_CAN_Body.alive_cnt = payload[0];
    Last_Body_RxTime = Get_SystemTime_ms();
    DCM_Callback_BodyRecovered();
}

uint8_t COM_Get_Ignition(void) { return COM_RxBuf_UDP_Ctr.ignition; }
uint8_t COM_Get_CurMode(void) { return COM_Cur_Mode; }
uint8_t COM_Get_BodyAliveCnt(void) { return COM_RxBuf_CAN_Body.alive_cnt; }
uint8_t COM_Get_Collision_Warn(void) { return COM_Collision_Warn; }
void COM_Set_CurMode(uint8_t cur_mode) { COM_Cur_Mode = cur_mode; }
void COM_Set_Safety_Override(uint8_t safety_override) { COM_Safety_Override = safety_override; }
void COM_Set_Turn_Signal(uint8_t turn_signal) { COM_RxBuf_UDP_Ctr.turn_signal = turn_signal; }
void COM_Set_Collision_Warn(uint8_t collision_warn) { COM_Collision_Warn = collision_warn; }

void COM_Tx_CtrAct_CAN(void)
{
    COM_TxBuf_CtrAct_CAN[0] = COM_RxBuf_UDP_Ctr.accel;
    COM_TxBuf_CtrAct_CAN[1] = COM_RxBuf_UDP_Ctr.steer;
    COM_TxBuf_CtrAct_CAN[2] = COM_RxBuf_UDP_Ctr.brake;
    COM_TxBuf_CtrAct_CAN[3] = COM_RxBuf_UDP_Ctr.gear;
    COM_TxBuf_CtrAct_CAN[4] = COM_Cur_Mode;
    COM_TxBuf_CtrAct_CAN[5] = COM_Safety_Override;
    //pdu로 보내기
    PduR_RouteTx(PDUR_TX_CAN_ACT_ID, COM_TxBuf_CtrAct_CAN, sizeof(COM_TxBuf_CtrAct_CAN));
}

void COM_Tx_CtrBody_CAN(void)
{
    COM_TxBuf_CtrBody_CAN[0] = COM_RxBuf_UDP_Ctr.head_light;
    COM_TxBuf_CtrBody_CAN[1] = COM_RxBuf_UDP_Ctr.turn_signal;
    COM_TxBuf_CtrBody_CAN[2] = COM_RxBuf_UDP_Ctr.brake;
    COM_TxBuf_CtrBody_CAN[3] = COM_RxBuf_UDP_Ctr.horn;
    COM_TxBuf_CtrBody_CAN[4] = COM_Collision_Warn;
    COM_TxBuf_CtrBody_CAN[5] = COM_Cur_Mode;
    //pdu로 보내기
    PduR_RouteTx(PDUR_TX_CAN_BODY_ID, COM_TxBuf_CtrBody_CAN, sizeof(COM_TxBuf_CtrBody_CAN));
}

void COM_Tx_Stat_UDP(void)
{
    COM_TxBuf_Stat_UDP[0] = COM_Stat_Cnt;
    COM_TxBuf_Stat_UDP[1] = COM_RxBuf_CAN_Stat.speedl;
    COM_TxBuf_Stat_UDP[2] = COM_RxBuf_CAN_Stat.speedh;
    //pdu로 전송
    Callback_COM_SessionCnt(&COM_Stat_Cnt);
    PduR_RouteTx(PDUR_TX_UDP_STAT_ID, COM_TxBuf_Stat_UDP, sizeof(COM_TxBuf_Stat_UDP));
}

//시간 보고 전체 전송하는 함수 하나
void COM_Tx_MainFunction(void)
{
    uint32_t currentTime = Get_SystemTime_ms();

    /* 정적 변수로 마지막 송신 시간을 기억 */
    static uint32_t lastTime_20ms = 0;
    static uint32_t lastTime_50ms = 0;

    /* 1. 20ms 주기 송신 (액추에이터 및 바디 제어 CAN) */
    if ((currentTime - lastTime_20ms) >= 20)
    {
        COM_Tx_CtrAct_CAN();
        COM_Tx_CtrBody_CAN();

        lastTime_20ms = currentTime;
    }

    /* 2. 50ms 주기 송신 (PC로 보내는 UDP 차량 상태) */
    if ((currentTime - lastTime_50ms) >= 50)
    {
        COM_Tx_Stat_UDP();

        lastTime_50ms = currentTime;
    }
}

void COM_TimeOut(void)
{
    uint32_t currentTime = Get_SystemTime_ms();

    /* 마지막 수신 시간과 현재 시간의 차이가 500ms 이상 벌어졌는지 확인 */
    if (COM_Cur_Mode == MODE_REMOTE && (currentTime - Last_Udp_RxTime) >= UDP_TIMEOUT_THRESHOLD_MS)
    {
        /* SWC로 "통신 끊겼다!" 라고 보고 (콜백 호출) */
        Callback_COM_PcTimeout();
        DCM_Callback_PcCommloss();
        Last_Udp_RxTime = currentTime;
    }

    if ((currentTime - Last_Act_RxTime) >= ACT_TIMEOUT_THRESHOLD_MS)
    {
        DCM_Callback_ActCommloss();
        Last_Act_RxTime = currentTime;
    }

    if ((currentTime - Last_Body_RxTime) >= BODY_TIMEOUT_THRESHOLD_MS)
    {
        DCM_Callback_BodyCommloss();
        Last_Body_RxTime = currentTime;
    }
}

static void SomeIp_SendResponse(uint8_t* rxPacket,uint8_t buzzerState)
{
    uint8_t tx[32];

    uint8_t* p = tx;

    /* =========================
     * SOME/IP Header
     * ========================= */

    *((uint16_t*)p) = lwip_htons(SOMEIP_SERVICE_ID);
    p += 2;

    *((uint16_t*)p) = lwip_htons(SOMEIP_METHOD_ID);
    p += 2;

    /*
     * Length:
     * Request ID ~ payload 끝
     *
     * 8 bytes header tail
     * 2 bytes payload
     */
    *((uint32_t*)p) = lwip_htonl(10);
    p += 4;

    /*
     * Client ID / Session ID
     * request 그대로 반사
     */
    memcpy(p, &rxPacket[8], 4);
    p += 4;

    /* Protocol Version */
    *p++ = 0x01;

    /* Interface Version */
    *p++ = 0x01;

    /* Message Type = Response */
    *p++ = 0x80;

    /* Return Code */
    *p++ = 0x00;

    /* =========================
     * Payload
     * ========================= */

    *p++ = 1;
    *p++ = buzzerState;

    /* =========================
     * 송신
     * ========================= */

    PduR_RouteTx(PDUR_TX_UDP_SOMEIP, tx, (uint16_t)(p - tx));
}
