/******************************************************************************
 * CAN-FD Driver (MCMCAN)
 *
 * NODE0
 *   TX : P20.8
 *   RX : P20.7
 *
 * NODE2
 *   TX : P15.0
 *   RX : P15.1
 *
 * Arbitration : 500K
 * Data Phase  : 2Mbps
 *
 ******************************************************************************/

#include "candrv.h"
#include "canif.h"

#include "IfxCan_Can.h"
#include "IfxPort.h"

#include <string.h>
#include <stdint.h>

/*============================================================================*/
/* CONFIG                                                                     */
/*============================================================================*/

#define CAN_RX_FIFO_SIZE      (8)

/*============================================================================*/
/* TYPES                                                                      */
/*============================================================================*/

typedef struct
{
    IfxCan_Can_Config     canConfig;
    IfxCan_Can            canModule;

    /* NODE0 */
    IfxCan_Can_NodeConfig nodeConfig0;
    IfxCan_Can_Node       canNode0;

    /* NODE2 */
    IfxCan_Can_NodeConfig nodeConfig2;
    IfxCan_Can_Node       canNode2;

} App_Mcmcan;

static App_Mcmcan g_mcmcan;

/*============================================================================*/
/* EXTERN                                                                     */
/*============================================================================*/

extern void CanIf_RxIndication(uint32_t canId,
                               uint8_t* payload,
                               uint8_t length);

/*============================================================================*/
/* DLC CONVERT                                                                */
/*============================================================================*/

static IfxCan_DataLengthCode Can_GetDlc(uint8 length)
{
    if(length <= 8)  return (IfxCan_DataLengthCode)length;
    if(length <= 12) return IfxCan_DataLengthCode_12;
    if(length <= 16) return IfxCan_DataLengthCode_16;
    if(length <= 20) return IfxCan_DataLengthCode_20;
    if(length <= 24) return IfxCan_DataLengthCode_24;
    if(length <= 32) return IfxCan_DataLengthCode_32;
    if(length <= 48) return IfxCan_DataLengthCode_48;

    return IfxCan_DataLengthCode_64;
}

static uint8 Can_GetLength(uint8 dlc)
{
    switch(dlc)
    {
        case 9:  return 12;
        case 10: return 16;
        case 11: return 20;
        case 12: return 24;
        case 13: return 32;
        case 14: return 48;
        case 15: return 64;

        default:
            return dlc;
    }
}

/*============================================================================*/
/* FILTER                                                                     */
/*============================================================================*/

static void Can_ConfigFilter(void)
{
    IfxCan_Filter filter;

    /*==================================================*/
    /* NODE0                                            */
    /*==================================================*/

    filter.elementConfiguration =
        IfxCan_FilterElementConfiguration_storeInRxFifo0;

    filter.type = IfxCan_FilterType_classic;

    filter.number = 0;
    filter.id1 = 0x200;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode0,
                                 &filter);

    filter.number = 1;
    filter.id1 = 0x708;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode0,
                                 &filter);

    /*==================================================*/
    /* NODE2                                            */
    /*==================================================*/

    filter.number = 0;
    filter.id1 = 0x210;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode2,
                                 &filter);

    filter.number = 1;
    filter.id1 = 0x310;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode2,
                                 &filter);

    filter.number = 2;
    filter.id1 = 0x718;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode2,
                                 &filter);
}

/*============================================================================*/
/* INIT                                                                       */
/*============================================================================*/

void Can_Init(void)
{
    /*--------------------------------------------------*/
    /* Transceiver enable                               */
    /*--------------------------------------------------*/

    IfxPort_setPinModeOutput(&MODULE_P20,
                             6,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    MODULE_P20.OUT.B.P6 = 0;

    /*--------------------------------------------------*/
    /* CAN MODULE INIT                                  */
    /*--------------------------------------------------*/

    IfxCan_Can_initModuleConfig(&g_mcmcan.canConfig,
                                &MODULE_CAN0);

    IfxCan_Can_initModule(&g_mcmcan.canModule,
                          &g_mcmcan.canConfig);

    /*==================================================*/
    /* NODE0 INIT                                       */
    /*==================================================*/

    IfxCan_Can_initNodeConfig(&g_mcmcan.nodeConfig0,
                              &g_mcmcan.canModule);

    g_mcmcan.nodeConfig0.nodeId = IfxCan_NodeId_0;

    g_mcmcan.nodeConfig0.baudRate.baudrate = 500000;

    g_mcmcan.nodeConfig0.fastBaudRate.baudrate = 2000000;

    static const IfxCan_Can_Pins canPins0 =
    {
        &IfxCan_TXD00_P20_8_OUT,
        IfxPort_OutputMode_pushPull,

        &IfxCan_RXD00B_P20_7_IN,
        IfxPort_InputMode_pullUp,

        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    g_mcmcan.nodeConfig0.pins = &canPins0;

    g_mcmcan.nodeConfig0.frame.type =
        IfxCan_FrameType_transmitAndReceive;

    g_mcmcan.nodeConfig0.frame.mode =
        IfxCan_FrameMode_fdLongAndFast;

    g_mcmcan.nodeConfig0.txConfig.txMode =
        IfxCan_TxMode_dedicatedBuffers;

    g_mcmcan.nodeConfig0.txConfig.txBufferDataFieldSize =
        IfxCan_DataFieldSize_64;

    g_mcmcan.nodeConfig0.rxConfig.rxMode =
        IfxCan_RxMode_fifo0;

    g_mcmcan.nodeConfig0.rxConfig.rxFifo0Size =
        CAN_RX_FIFO_SIZE;

    g_mcmcan.nodeConfig0.rxConfig.rxFifo0DataFieldSize =
        IfxCan_DataFieldSize_64;

    g_mcmcan.nodeConfig0.rxConfig.rxBufferDataFieldSize =
        IfxCan_DataFieldSize_64;

    g_mcmcan.nodeConfig0.filterConfig.messageIdLength =
        IfxCan_MessageIdLength_standard;

    g_mcmcan.nodeConfig0.filterConfig.standardListSize = 3;

    g_mcmcan.nodeConfig0.filterConfig.standardFilterForNonMatchingFrames =
        IfxCan_NonMatchingFrame_reject;

    g_mcmcan.nodeConfig0.filterConfig.rejectRemoteFramesWithStandardId =
        TRUE;

    /*==================================================*/
    /* NODE0 RAM                                        */
    /*==================================================*/

    g_mcmcan.nodeConfig0.messageRAM.baseAddress =
        (uint32)CAN0_RAM;

    g_mcmcan.nodeConfig0.messageRAM.standardFilterListStartAddress = 0x000;
    g_mcmcan.nodeConfig0.messageRAM.extendedFilterListStartAddress = 0x040;

    g_mcmcan.nodeConfig0.messageRAM.rxFifo0StartAddress            = 0x080;
    g_mcmcan.nodeConfig0.messageRAM.rxFifo1StartAddress            = 0x200;

    g_mcmcan.nodeConfig0.messageRAM.rxBuffersStartAddress          = 0x300;

    g_mcmcan.nodeConfig0.messageRAM.txEventFifoStartAddress        = 0x400;

    g_mcmcan.nodeConfig0.messageRAM.txBuffersStartAddress          = 0x500;

    IfxCan_Can_initNode(&g_mcmcan.canNode0,
                        &g_mcmcan.nodeConfig0);

    /*==================================================*/
    /* NODE2 INIT                                       */
    /*==================================================*/

    IfxCan_Can_initNodeConfig(&g_mcmcan.nodeConfig2,
                              &g_mcmcan.canModule);

    g_mcmcan.nodeConfig2.nodeId = IfxCan_NodeId_2;

    g_mcmcan.nodeConfig2.baudRate.baudrate = 500000;

    g_mcmcan.nodeConfig2.fastBaudRate.baudrate = 2000000;

    static const IfxCan_Can_Pins canPins2 =
    {
        &IfxCan_TXD02_P15_0_OUT,
        IfxPort_OutputMode_pushPull,

        &IfxCan_RXD02A_P15_1_IN,
        IfxPort_InputMode_pullUp,

        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    g_mcmcan.nodeConfig2.pins = &canPins2;

    g_mcmcan.nodeConfig2.frame.type =
        IfxCan_FrameType_transmitAndReceive;

    g_mcmcan.nodeConfig2.frame.mode =
        IfxCan_FrameMode_fdLongAndFast;

    g_mcmcan.nodeConfig2.txConfig.txMode =
        IfxCan_TxMode_dedicatedBuffers;

    g_mcmcan.nodeConfig2.txConfig.txBufferDataFieldSize =
        IfxCan_DataFieldSize_64;

    g_mcmcan.nodeConfig2.rxConfig.rxMode =
        IfxCan_RxMode_fifo0;

    g_mcmcan.nodeConfig2.rxConfig.rxFifo0Size =
        CAN_RX_FIFO_SIZE;

    g_mcmcan.nodeConfig2.rxConfig.rxFifo0DataFieldSize =
        IfxCan_DataFieldSize_64;

    g_mcmcan.nodeConfig2.rxConfig.rxBufferDataFieldSize =
        IfxCan_DataFieldSize_64;

    g_mcmcan.nodeConfig2.filterConfig.messageIdLength =
        IfxCan_MessageIdLength_standard;

    g_mcmcan.nodeConfig2.filterConfig.standardListSize = 3;

    g_mcmcan.nodeConfig2.filterConfig.standardFilterForNonMatchingFrames =
        IfxCan_NonMatchingFrame_reject;

    g_mcmcan.nodeConfig2.filterConfig.rejectRemoteFramesWithStandardId =
        TRUE;

    /*==================================================*/
    /* NODE2 RAM                                        */
    /*==================================================*/

    g_mcmcan.nodeConfig2.messageRAM.baseAddress =
        (uint32)CAN0_RAM;

    g_mcmcan.nodeConfig2.messageRAM.standardFilterListStartAddress = 0x800;
    g_mcmcan.nodeConfig2.messageRAM.extendedFilterListStartAddress = 0x840;

    g_mcmcan.nodeConfig2.messageRAM.rxFifo0StartAddress            = 0x880;
    g_mcmcan.nodeConfig2.messageRAM.rxFifo1StartAddress            = 0xA00;

    g_mcmcan.nodeConfig2.messageRAM.rxBuffersStartAddress          = 0xB00;

    g_mcmcan.nodeConfig2.messageRAM.txEventFifoStartAddress        = 0xC00;

    g_mcmcan.nodeConfig2.messageRAM.txBuffersStartAddress          = 0xD00;

    IfxCan_Can_initNode(&g_mcmcan.canNode2,
                        &g_mcmcan.nodeConfig2);

    /*--------------------------------------------------*/
    /* FILTER APPLY                                     */
    /*--------------------------------------------------*/

    Can_ConfigFilter();
}

/*============================================================================*/
/* TX                                                                         */
/*============================================================================*/

boolean Can_Send(uint32_t canId,
                 uint8_t* data,
                 uint8_t length)
{
    IfxCan_Message msg;

    uint32 messageData[16] = {0};

    IfxCan_Can_Node* txNode = NULL_PTR;

    if(length > 64)
    {
        return FALSE;
    }

    IfxCan_Can_initMessage(&msg);

    msg.messageId = canId;

    msg.messageIdLength =
        IfxCan_MessageIdLength_standard;

    msg.frameMode =
        IfxCan_FrameMode_fdLongAndFast;

    msg.dataLengthCode =
        Can_GetDlc(length);

    memcpy(messageData,
           data,
           length);

    /*--------------------------------------------------*/
    /* TX NODE SELECT                                   */
    /*--------------------------------------------------*/

    if((canId == 0x100) ||
       (canId == 0x700))
    {
        txNode = &g_mcmcan.canNode0;
    }
    else if((canId == 0x110) ||
            (canId == 0x710))
    {
        txNode = &g_mcmcan.canNode2;
    }
    else
    {
        return FALSE;
    }

    /*--------------------------------------------------*/
    /* SEND                                             */
    /*--------------------------------------------------*/

    if(IfxCan_Can_sendMessage(txNode,
                              &msg,
                              messageData)
            == IfxCan_Status_notSentBusy)
    {
        return FALSE;
    }

    return TRUE;
}

/*============================================================================*/
/* RX                                                                         */
/*============================================================================*/

static void Can_ReadNode(IfxCan_Can_Node* node)
{
    while(IfxCan_Can_getRxFifo0FillLevel(node) > 0)
    {
        IfxCan_Message msg;

        uint32 messageData[16];

        uint8 payload[64];

        uint8 length;

        IfxCan_Can_initMessage(&msg);

        msg.readFromRxFifo0 = TRUE;

        IfxCan_Can_readMessage(node,
                               &msg,
                               messageData);

        length = Can_GetLength(msg.dataLengthCode);

        memcpy(payload,
               messageData,
               length);

        CanIf_RxIndication(msg.messageId,
                           payload,
                           length);
    }
}

void Can_Read(void)
{
    Can_ReadNode(&g_mcmcan.canNode0);

    Can_ReadNode(&g_mcmcan.canNode2);
}
