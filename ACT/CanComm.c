#include "CanComm.h"
#include "ActCan.h"

#include "Ifx_Types.h"
#include "IfxCan_Can.h"
#include "IfxCan_PinMap.h"
#include "IfxPort.h"

/*
 * ======================================================
 * AURIX TC375 Lite Kit CANH/CANL connector near Ethernet
 *
 * CAN connector:
 * CANH / CANL
 *
 * MCU side:
 * CAN0 Node0
 * TX  = P20.8 = IfxCan_TXD00_P20_8_OUT
 * RX  = P20.7 = IfxCan_RXD00B_P20_7_IN
 * STB = P20.6 = LOW to enable CAN transceiver
 *
 * PCAN-View:
 * Classical CAN
 * Standard ID
 * 500 kbit/s
 * ======================================================
 */

#define CAN_BAUDRATE              (500000U)
#define CAN_TX_ID                 (ACT_STATUS_CAN_ID)

#define CAN_MODULE_RAM_BASE       (0xF0200000U)
#define CAN_TX_BUFFER_COUNT       (2U)

/*
 * Lite Kit CAN transceiver standby pin
 * LOW = normal mode
 * HIGH = standby mode
 */
#define CAN_STB_PORT              (&MODULE_P20)
#define CAN_STB_PIN               (6U)

/*
 * CRC-8/SAE-J1850
 * Polynomial = 0x1D
 * Initial    = 0xFF
 * XorOut     = 0xFF
 * No reflection
 *
 * ActStatusMsg에서는 Byte5에 CRC를 넣기 때문에
 * CRC 계산 대상은 Byte0~Byte4이다.
 */
#define ACT_STATUS_CRC8_POLY      (0x1DU)
#define ACT_STATUS_CRC8_INIT      (0xFFU)
#define ACT_STATUS_CRC8_XOROUT    (0xFFU)
#define ACT_STATUS_CRC8_DATA_LEN  (5U)

static IfxCan_Can        g_canModule;
static IfxCan_Can_Node   g_canNode;

volatile uint32 g_debugCanTxCallCount = 0U;
volatile uint32 g_debugCanTxOkCount = 0U;
volatile uint32 g_debugCanTxBusyCount = 0U;
volatile uint32 g_debugCanLastStatus = 0U;

volatile uint8 g_debugActStatusAliveCounter = 0U;
volatile uint8 g_debugActStatusLastCrc = 0U;

static uint8 g_txBufferIndex = 0U;
static uint8 g_actStatusAliveCounter = 0U;

static const IfxCan_Can_Pins g_canPins =
{
    &IfxCan_TXD00_P20_8_OUT,
    IfxPort_OutputMode_pushPull,

    &IfxCan_RXD00B_P20_7_IN,
    IfxPort_InputMode_pullUp,

    IfxPort_PadDriver_cmosAutomotiveSpeed4
};

static void CanComm_EnableTransceiver(void)
{
    IfxPort_setPinModeOutput(CAN_STB_PORT,
                             CAN_STB_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    /*
     * CAN transceiver normal mode
     */
    IfxPort_setPinLow(CAN_STB_PORT, CAN_STB_PIN);
}

static uint8 CanComm_CalcCrc8(const uint8* data, uint32 length)
{
    uint32 i;
    uint8 bit;
    uint8 crc;

    crc = ACT_STATUS_CRC8_INIT;

    for (i = 0U; i < length; i++)
    {
        crc = (uint8)(crc ^ data[i]);

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = (uint8)((crc << 1U) ^ ACT_STATUS_CRC8_POLY);
            }
            else
            {
                crc = (uint8)(crc << 1U);
            }
        }
    }

    crc = (uint8)(crc ^ ACT_STATUS_CRC8_XOROUT);

    return crc;
}

static uint8 CanComm_LimitGearState(uint8 gearState)
{
    if ((gearState == ACT_STATUS_GEAR_P) ||
        (gearState == ACT_STATUS_GEAR_R) ||
        (gearState == ACT_STATUS_GEAR_N) ||
        (gearState == ACT_STATUS_GEAR_D))
    {
        return gearState;
    }

    return ACT_STATUS_GEAR_P;
}

static uint8 CanComm_LimitSteeringState(uint8 steeringState)
{
    if ((steeringState == ACT_STATUS_STEERING_LEFT) ||
        (steeringState == ACT_STATUS_STEERING_CENTER) ||
        (steeringState == ACT_STATUS_STEERING_RIGHT))
    {
        return steeringState;
    }

    return ACT_STATUS_STEERING_CENTER;
}

void CanComm_Init(void)
{
    IfxCan_Can_Config canConfig;
    IfxCan_Can_NodeConfig nodeConfig;

    /*
     * 0. Enable external CAN transceiver
     */
    CanComm_EnableTransceiver();

    /*
     * 1. CAN0 module init
     */
    IfxCan_Can_initModuleConfig(&canConfig, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_canModule, &canConfig);

    /*
     * 2. CAN0 Node0 init
     */
    IfxCan_Can_initNodeConfig(&nodeConfig, &g_canModule);

    nodeConfig.nodeId = IfxCan_NodeId_0;
    nodeConfig.clockSource = IfxCan_ClockSource_both;

    nodeConfig.baudRate.baudrate = CAN_BAUDRATE;

    nodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    nodeConfig.frame.mode = IfxCan_FrameMode_standard;

    /*
     * Dedicated TX buffer
     */
    nodeConfig.txConfig.txMode = IfxCan_TxMode_dedicatedBuffers;
    nodeConfig.txConfig.dedicatedTxBuffersNumber = CAN_TX_BUFFER_COUNT;
    nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_8;

    /*
     * RX는 지금 안 써도 기본 8 byte로 잡아둠
     */
    nodeConfig.rxConfig.rxMode = IfxCan_RxMode_dedicatedBuffers;
    nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_8;

    /*
     * CAN0 Message RAM layout
     */
    nodeConfig.messageRAM.baseAddress = CAN_MODULE_RAM_BASE;
    nodeConfig.messageRAM.standardFilterListStartAddress = 0x000U;
    nodeConfig.messageRAM.rxBuffersStartAddress = 0x100U;
    nodeConfig.messageRAM.txBuffersStartAddress = 0x200U;

    nodeConfig.pins = &g_canPins;

    IfxCan_Can_initNode(&g_canNode, &nodeConfig);

    g_debugCanTxCallCount = 0U;
    g_debugCanTxOkCount = 0U;
    g_debugCanTxBusyCount = 0U;
    g_debugCanLastStatus = 0U;

    g_debugActStatusAliveCounter = 0U;
    g_debugActStatusLastCrc = 0U;

    g_txBufferIndex = 0U;
    g_actStatusAliveCounter = 0U;
}

void CanComm_SendActStatus(uint32 speedKmhX100,
                           uint8 gearState,
                           uint8 steeringState)
{
    IfxCan_Message txMsg;
    uint32 txData[2];
    uint8 txByte[8];
    uint16 speed16;
    uint8 crc8;
    uint8 aliveCounter;
    IfxCan_Status status;

    g_debugCanTxCallCount++;

    if (speedKmhX100 > 65535U)
    {
        speedKmhX100 = 65535U;
    }

    speed16 = (uint16)speedKmhX100;
    gearState = CanComm_LimitGearState(gearState);
    steeringState = CanComm_LimitSteeringState(steeringState);
    aliveCounter = g_actStatusAliveCounter;

    /*
     * ActStatusMsg 0x200
     *
     * Byte 0~1 : speed_kmh_x100, Little Endian
     * Byte 2   : gear_state
     * Byte 3   : steering_state
     * Byte 4   : alive_counter
     * Byte 5   : crc8 over Byte0~Byte4
     * Byte 6~7 : reserved 0x00
     */
    txByte[0] = (uint8)(speed16 & 0x00FFU);
    txByte[1] = (uint8)((speed16 >> 8U) & 0x00FFU);
    txByte[2] = gearState;
    txByte[3] = steeringState;
    txByte[4] = aliveCounter;
    txByte[5] = 0U;
    txByte[6] = 0U;
    txByte[7] = 0U;

    crc8 = CanComm_CalcCrc8(txByte, ACT_STATUS_CRC8_DATA_LEN);
    txByte[5] = crc8;

    txData[0] = 0U;
    txData[1] = 0U;

    txData[0] |= ((uint32)txByte[0]) << 0U;
    txData[0] |= ((uint32)txByte[1]) << 8U;
    txData[0] |= ((uint32)txByte[2]) << 16U;
    txData[0] |= ((uint32)txByte[3]) << 24U;

    txData[1] |= ((uint32)txByte[4]) << 0U;
    txData[1] |= ((uint32)txByte[5]) << 8U;
    txData[1] |= ((uint32)txByte[6]) << 16U;
    txData[1] |= ((uint32)txByte[7]) << 24U;

    IfxCan_Can_initMessage(&txMsg);

    txMsg.messageId = CAN_TX_ID;
    txMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    txMsg.frameMode = IfxCan_FrameMode_standard;
    txMsg.dataLengthCode = IfxCan_DataLengthCode_8;

    txMsg.bufferNumber = g_txBufferIndex;
    txMsg.storeInTxFifoQueue = FALSE;

    /*
     * 현재 최종 통합 구조에서는 ActCan_Init()이 CAN0 Node0을 초기화한다.
     * 따라서 송신도 ActCan_GetCanNode()로 같은 Node를 사용한다.
     */
    status = IfxCan_Can_sendMessage(ActCan_GetCanNode(), &txMsg, txData);

    g_debugCanLastStatus = (uint32)status;

    if (status == IfxCan_Status_notSentBusy)
    {
        g_debugCanTxBusyCount++;
        return;
    }

    /*
     * 실제 송신 성공 시에만 alive_counter 증가.
     * 255 다음에는 uint8 overflow로 0으로 순환한다.
     */
    g_actStatusAliveCounter++;

    g_debugActStatusAliveCounter = g_actStatusAliveCounter;
    g_debugActStatusLastCrc = crc8;

    g_debugCanTxOkCount++;

    g_txBufferIndex++;

    if (g_txBufferIndex >= CAN_TX_BUFFER_COUNT)
    {
        g_txBufferIndex = 0U;
    }
}
