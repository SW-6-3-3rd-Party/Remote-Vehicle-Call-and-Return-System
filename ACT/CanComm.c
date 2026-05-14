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
#define CAN_TX_ID                 (0x322U)

#define CAN_MODULE_RAM_BASE       (0xF0200000U)
#define CAN_TX_BUFFER_COUNT       (2U)

/*
 * Lite Kit CAN transceiver standby pin
 * LOW = normal mode
 * HIGH = standby mode
 */
#define CAN_STB_PORT              (&MODULE_P20)
#define CAN_STB_PIN               (6U)

static IfxCan_Can        g_canModule;
static IfxCan_Can_Node   g_canNode;

volatile uint32 g_debugCanTxCallCount = 0U;
volatile uint32 g_debugCanTxOkCount = 0U;
volatile uint32 g_debugCanTxBusyCount = 0U;
volatile uint32 g_debugCanLastStatus = 0U;

static uint8 g_txBufferIndex = 0U;

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
     * Important:
     * CAN transceiver normal mode
     */
    IfxPort_setPinLow(CAN_STB_PORT, CAN_STB_PIN);
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
    g_txBufferIndex = 0U;
}

void CanComm_SendDriveStatus(MotorState state,
                             uint32 dutyPercent,
                             uint32 speedKmhX100,
                             uint32 pulsePerSecond,
                             sint32 encoderCount)
{
    IfxCan_Message txMsg;
    uint32 txData[2];

    uint16 speed16;
    uint16 pps16;
    sint16 cnt16;
    IfxCan_Status status;

    g_debugCanTxCallCount++;

    if (dutyPercent > 100U)
    {
        dutyPercent = 100U;
    }

    if (speedKmhX100 > 65535U)
    {
        speedKmhX100 = 65535U;
    }

    if (pulsePerSecond > 65535U)
    {
        pulsePerSecond = 65535U;
    }

    if (encoderCount > 32767)
    {
        encoderCount = 32767;
    }
    else if (encoderCount < -32768)
    {
        encoderCount = -32768;
    }

    speed16 = (uint16)speedKmhX100;
    pps16 = (uint16)pulsePerSecond;
    cnt16 = (sint16)encoderCount;

    txData[0] = 0U;
    txData[1] = 0U;

    /*
     * Byte0 = state
     * Byte1 = duty
     * Byte2~3 = rpm
     * Byte4~5 = pulse/sec
     * Byte6~7 = encoder count
     */
    txData[0] |= ((uint32)((uint8)state) & 0xFFU) << 0U;
    txData[0] |= ((uint32)((uint8)dutyPercent) & 0xFFU) << 8U;
    txData[0] |= ((uint32)(speed16 & 0x00FFU)) << 16U;
    txData[0] |= ((uint32)((speed16 >> 8U) & 0x00FFU)) << 24U;

    txData[1] |= ((uint32)(pps16 & 0x00FFU)) << 0U;
    txData[1] |= ((uint32)((pps16 >> 8U) & 0x00FFU)) << 8U;
    txData[1] |= ((uint32)(((uint16)cnt16) & 0x00FFU)) << 16U;
    txData[1] |= ((uint32)((((uint16)cnt16) >> 8U) & 0x00FFU)) << 24U;

    IfxCan_Can_initMessage(&txMsg);

    txMsg.messageId = CAN_TX_ID;
    txMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    txMsg.frameMode = IfxCan_FrameMode_standard;
    txMsg.dataLengthCode = IfxCan_DataLengthCode_8;

    txMsg.bufferNumber = g_txBufferIndex;
    txMsg.storeInTxFifoQueue = FALSE;

    status = IfxCan_Can_sendMessage(ActCan_GetCanNode(), &txMsg, txData);

    g_debugCanLastStatus = (uint32)status;

    if (status == IfxCan_Status_notSentBusy)
    {
        g_debugCanTxBusyCount++;
        return;
    }

    g_debugCanTxOkCount++;

    g_txBufferIndex++;

    if (g_txBufferIndex >= CAN_TX_BUFFER_COUNT)
    {
        g_txBufferIndex = 0U;
    }
}
