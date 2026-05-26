#include "CanComm.h"
#include "ActCan.h"
#include "PotAdc.h"

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
 * CAN FD with bitrate switching
 * Standard ID
 * Nominal phase: 500 kbit/s
 * Data phase   : 2 Mbit/s
 * ======================================================
 */

#define CAN_BAUDRATE              (500000U)
#define CAN_FAST_BAUDRATE         (2000000U)
#define CAN_TX_ID                 (ACT_STATUS_CAN_ID)

#define CAN_MODULE_RAM_BASE       (0xF0200000U)
#define CAN_TX_BUFFER_COUNT       (2U)

#define CAN_STB_PORT              (&MODULE_P20)
#define CAN_STB_PIN               (6U)

static IfxCan_Can        g_canModule;
static IfxCan_Can_Node   g_canNode;

volatile uint32 g_debugCanTxCallCount = 0U;
volatile uint32 g_debugCanTxOkCount = 0U;
volatile uint32 g_debugCanTxBusyCount = 0U;
volatile uint32 g_debugCanLastStatus = 0U;
volatile uint8 g_debugActStatusLastSteeringAngleDeg = 0U;

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

    /* CAN transceiver normal mode: LOW */
    IfxPort_setPinLow(CAN_STB_PORT, CAN_STB_PIN);
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

static uint8 CanComm_LimitSteeringAngleDeg(uint8 steeringAngleDeg)
{
    return steeringAngleDeg;
}

void CanComm_Init(void)
{
    IfxCan_Can_Config canConfig;
    IfxCan_Can_NodeConfig nodeConfig;

    CanComm_EnableTransceiver();

    IfxCan_Can_initModuleConfig(&canConfig, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_canModule, &canConfig);

    IfxCan_Can_initNodeConfig(&nodeConfig, &g_canModule);

    nodeConfig.nodeId = IfxCan_NodeId_0;
    nodeConfig.clockSource = IfxCan_ClockSource_both;
    nodeConfig.baudRate.baudrate = CAN_BAUDRATE;
    nodeConfig.fastBaudRate.baudrate = CAN_FAST_BAUDRATE;
    nodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    nodeConfig.frame.mode = IfxCan_FrameMode_fdLongAndFast;

    nodeConfig.txConfig.txMode = IfxCan_TxMode_dedicatedBuffers;
    nodeConfig.txConfig.dedicatedTxBuffersNumber = CAN_TX_BUFFER_COUNT;
    nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_8;

    nodeConfig.rxConfig.rxMode = IfxCan_RxMode_dedicatedBuffers;
    nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_8;

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
    g_debugActStatusLastSteeringAngleDeg = 0U;

    g_txBufferIndex = 0U;
}

void CanComm_SendActStatus(uint32 speedKmhX100,
                           uint8 gearState,
                           uint8 steeringState,
                           uint8 steeringAngleDeg)
{
    IfxCan_Message txMsg;
    uint32 txData[2];
    uint8 txByte[5];
    uint16 speed16;
    IfxCan_Status status;

    g_debugCanTxCallCount++;

    if (speedKmhX100 > 65535U)
    {
        speedKmhX100 = 65535U;
    }

    speed16 = (uint16)speedKmhX100;
    gearState = CanComm_LimitGearState(gearState);
    steeringState = CanComm_LimitSteeringState(steeringState);
    steeringAngleDeg = CanComm_LimitSteeringAngleDeg(steeringAngleDeg);

    /*
     * ActStatusMsg, ACT -> MAIN, 50ms
     * CAN ID 0x200, DLC 5
     *
     * Byte0 speed_L
     * Byte1 speed_H
     * Byte2 gear_state
     * Byte3 steering_state
     * Byte4 steering_angle
     */
    txByte[0] = (uint8)(speed16 & 0x00FFU);
    txByte[1] = (uint8)((speed16 >> 8U) & 0x00FFU);
    txByte[2] = gearState;
    txByte[3] = steeringState;
    txByte[4] = steeringAngleDeg;

    txData[0] = 0U;
    txData[1] = 0U;

    txData[0] |= ((uint32)txByte[0]) << 0U;
    txData[0] |= ((uint32)txByte[1]) << 8U;
    txData[0] |= ((uint32)txByte[2]) << 16U;
    txData[0] |= ((uint32)txByte[3]) << 24U;

    txData[1] |= ((uint32)txByte[4]) << 0U;

    IfxCan_Can_initMessage(&txMsg);

    txMsg.messageId = ACT_STATUS_CAN_ID;
    txMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    txMsg.frameMode = IfxCan_FrameMode_fdLongAndFast;
    txMsg.dataLengthCode = ACT_STATUS_CAN_DLC_IFX;

    txMsg.bufferNumber = g_txBufferIndex;
    txMsg.storeInTxFifoQueue = FALSE;

    /*
     * Final integration uses ActCan_Init() only.
     * Therefore, transmit through the same CAN0 Node0 initialized by ActCan.
     */
    status = IfxCan_Can_sendMessage(ActCan_GetCanNode(), &txMsg, txData);

    g_debugCanLastStatus = (uint32)status;

    if (status == IfxCan_Status_notSentBusy)
    {
        g_debugCanTxBusyCount++;
        return;
    }

    g_debugActStatusLastSteeringAngleDeg = steeringAngleDeg;
    g_debugCanTxOkCount++;

    g_txBufferIndex++;

    if (g_txBufferIndex >= CAN_TX_BUFFER_COUNT)
    {
        g_txBufferIndex = 0U;
    }
}
