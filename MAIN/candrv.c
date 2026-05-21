/******************************************************************************
 * CAN Driver (MCMCAN Node0)
 *
 * RX : 0x200, 0x708
 * TX : 0x100, 0x700
 *
 * RX Path:
 *   MCMCAN FIFO0
 *      ↓
 *   Can_MainFunction_Read()
 *      ↓
 *   CanIf_RxIndication()
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

    /* CAN0 Node0 */
    IfxCan_Can_NodeConfig nodeConfig0;
    IfxCan_Can_Node       canNode0;

    /* CAN0 Node2 */
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
/* RX FILTER                                                                  */
/*============================================================================*/

static void Can_ConfigFilter(void)
{
    IfxCan_Filter filter;

    /*==================================================*/
    /* NODE0 FILTER                                     */
    /*==================================================*/

    /*--------------------------------------------------*/
    /* RX : 0x200                                       */
    /*--------------------------------------------------*/
    filter.number = 0;

    filter.elementConfiguration =
        IfxCan_FilterElementConfiguration_storeInRxFifo0;

    filter.type = IfxCan_FilterType_classic;

    filter.id1 = 0x200;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode0,
                                 &filter);

    /*--------------------------------------------------*/
    /* RX : 0x708                                       */
    /*--------------------------------------------------*/
    filter.number = 1;

    filter.id1 = 0x708;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode0,
                                 &filter);

    /*==================================================*/
    /* NODE2 FILTER                                     */
    /*==================================================*/

    /*--------------------------------------------------*/
    /* RX : 0x210                                       */
    /*--------------------------------------------------*/
    filter.number = 0;

    filter.elementConfiguration =
        IfxCan_FilterElementConfiguration_storeInRxFifo0;

    filter.type = IfxCan_FilterType_classic;

    filter.id1 = 0x210;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode2,
                                 &filter);

    /*--------------------------------------------------*/
    /* RX : 0x310                                       */
    /*--------------------------------------------------*/
    filter.number = 1;

    filter.id1 = 0x310;
    filter.id2 = 0x7FF;

    IfxCan_Can_setStandardFilter(&g_mcmcan.canNode2,
                                 &filter);

    /*--------------------------------------------------*/
    /* RX : 0x718                                       */
    /*--------------------------------------------------*/
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
    /* CAN module init                                  */
    /*--------------------------------------------------*/
    IfxCan_Can_initModuleConfig(&g_mcmcan.canConfig,
                                &MODULE_CAN0);

    IfxCan_Can_initModule(&g_mcmcan.canModule,
                          &g_mcmcan.canConfig);

    /*==================================================*/
    /* NODE0 INIT (P20.7 / P20.8)                       */
    /*==================================================*/

    IfxCan_Can_initNodeConfig(&g_mcmcan.nodeConfig0,
                              &g_mcmcan.canModule);

    g_mcmcan.nodeConfig0.nodeId = IfxCan_NodeId_0;

    g_mcmcan.nodeConfig0.baudRate.baudrate = 500000;

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

    g_mcmcan.nodeConfig0.rxConfig.rxMode =
        IfxCan_RxMode_fifo0;

    g_mcmcan.nodeConfig0.rxConfig.rxFifo0Size =
        CAN_RX_FIFO_SIZE;

    g_mcmcan.nodeConfig0.rxConfig.rxFifo0DataFieldSize =
        IfxCan_DataFieldSize_8;

    g_mcmcan.nodeConfig0.rxConfig.rxBufferDataFieldSize =
        IfxCan_DataFieldSize_8;

    g_mcmcan.nodeConfig0.filterConfig.messageIdLength =
        IfxCan_MessageIdLength_standard;

    g_mcmcan.nodeConfig0.filterConfig.standardListSize = 3;

    g_mcmcan.nodeConfig0.filterConfig.standardFilterForNonMatchingFrames =
        IfxCan_NonMatchingFrame_reject;

    g_mcmcan.nodeConfig0.filterConfig.rejectRemoteFramesWithStandardId =
        TRUE;

    /*==================================================*/
    /* NODE0 MESSAGE RAM                                */
    /*==================================================*/

    g_mcmcan.nodeConfig0.messageRAM.baseAddress =
        (uint32)CAN0_RAM;

    g_mcmcan.nodeConfig0.messageRAM.standardFilterListStartAddress = 0x000;
    g_mcmcan.nodeConfig0.messageRAM.extendedFilterListStartAddress = 0x040;

    g_mcmcan.nodeConfig0.messageRAM.rxFifo0StartAddress            = 0x080;
    g_mcmcan.nodeConfig0.messageRAM.rxFifo1StartAddress            = 0x0C0;

    g_mcmcan.nodeConfig0.messageRAM.rxBuffersStartAddress          = 0x100;

    g_mcmcan.nodeConfig0.messageRAM.txEventFifoStartAddress        = 0x140;

    g_mcmcan.nodeConfig0.messageRAM.txBuffersStartAddress          = 0x180;

    IfxCan_Can_initNode(&g_mcmcan.canNode0,
                        &g_mcmcan.nodeConfig0);

    /*==================================================*/
    /* NODE2 INIT (P10.2 / P10.3)                       */
    /*==================================================*/

    IfxCan_Can_initNodeConfig(&g_mcmcan.nodeConfig2,
                              &g_mcmcan.canModule);

    g_mcmcan.nodeConfig2.nodeId = IfxCan_NodeId_2;

    g_mcmcan.nodeConfig2.baudRate.baudrate = 500000;

    static const IfxCan_Can_Pins canPins2 =
    {
        &IfxCan_TXD02_P10_3_OUT,
        IfxPort_OutputMode_pushPull,

        &IfxCan_RXD02E_P10_2_IN,
        IfxPort_InputMode_pullUp,

        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    g_mcmcan.nodeConfig2.pins = &canPins2;

    g_mcmcan.nodeConfig2.frame.type =
        IfxCan_FrameType_transmitAndReceive;

    g_mcmcan.nodeConfig2.rxConfig.rxMode =
        IfxCan_RxMode_fifo0;

    g_mcmcan.nodeConfig2.rxConfig.rxFifo0Size =
        CAN_RX_FIFO_SIZE;

    g_mcmcan.nodeConfig2.rxConfig.rxFifo0DataFieldSize =
        IfxCan_DataFieldSize_8;

    g_mcmcan.nodeConfig2.rxConfig.rxBufferDataFieldSize =
        IfxCan_DataFieldSize_8;

    g_mcmcan.nodeConfig2.filterConfig.messageIdLength =
        IfxCan_MessageIdLength_standard;

    g_mcmcan.nodeConfig2.filterConfig.standardListSize = 2;

    g_mcmcan.nodeConfig2.filterConfig.standardFilterForNonMatchingFrames =
        IfxCan_NonMatchingFrame_reject;

    g_mcmcan.nodeConfig2.filterConfig.rejectRemoteFramesWithStandardId =
        TRUE;

    /*==================================================*/
    /* NODE2 MESSAGE RAM                                */
    /*==================================================*/

    g_mcmcan.nodeConfig2.messageRAM.baseAddress =
        (uint32)CAN0_RAM;

    g_mcmcan.nodeConfig2.messageRAM.standardFilterListStartAddress = 0x200;
    g_mcmcan.nodeConfig2.messageRAM.extendedFilterListStartAddress = 0x240;

    g_mcmcan.nodeConfig2.messageRAM.rxFifo0StartAddress            = 0x280;
    g_mcmcan.nodeConfig2.messageRAM.rxFifo1StartAddress            = 0x2C0;

    g_mcmcan.nodeConfig2.messageRAM.rxBuffersStartAddress          = 0x300;

    g_mcmcan.nodeConfig2.messageRAM.txEventFifoStartAddress        = 0x340;

    g_mcmcan.nodeConfig2.messageRAM.txBuffersStartAddress          = 0x380;

    IfxCan_Can_initNode(&g_mcmcan.canNode2,
                        &g_mcmcan.nodeConfig2);

    /*--------------------------------------------------*/
    /* Filter apply                                     */
    /*--------------------------------------------------*/
    Can_ConfigFilter();
}

/*============================================================================*/
/* TX                                                                         */
/*============================================================================*/

boolean Can_Send(uint32_t canId,
                 uint8_t* data,
                 uint8_t dlc)
{
    IfxCan_Message msg;
    uint32 messageData[2] = {0};

    IfxCan_Can_Node* txNode = NULL_PTR;

    IfxCan_Can_initMessage(&msg);

    msg.messageId = canId;
    msg.dataLengthCode = dlc;
    msg.frameMode = IfxCan_FrameMode_standard;

    memcpy(messageData, data, dlc);

    /*--------------------------------------------------*/
    /* TX node select                                   */
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
    /* Send                                              */
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
/* RX POLLING                                                                 */
/*============================================================================*/

static void Can_ReadNode(IfxCan_Can_Node* node)
{
    while(IfxCan_Can_getRxFifo0FillLevel(node) > 0)
    {
        IfxCan_Message msg;
        uint32 messageData[2];
        uint8 payload[8];

        IfxCan_Can_initMessage(&msg);

        msg.readFromRxFifo0 = TRUE;

        IfxCan_Can_readMessage(node,
                               &msg,
                               messageData);

        memcpy(payload,
               messageData,
               msg.dataLengthCode);

        /*--------------------------------------------------*/
        /* Upper layer indication                           */
        /*--------------------------------------------------*/

        CanIf_RxIndication(msg.messageId,
                           payload,
                           msg.dataLengthCode);
    }
}

void Can_Read(void)
{
    Can_ReadNode(&g_mcmcan.canNode0);
    Can_ReadNode(&g_mcmcan.canNode2);
}
