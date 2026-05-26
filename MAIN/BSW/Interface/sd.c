#include "sd.h"
#include "soad.h"

#include "lwip/def.h"
#include "gettime.h"

#include <string.h>

/* =========================
 * SOME/IP-SD 설정값
 * ========================= */

#define SD_MULTICAST_IP        "224.224.224.245"
#define SD_MULTICAST_PORT      30490

#define SD_SERVICE_ID          0x2001
#define SD_INSTANCE_ID         0x0001

#define SD_MAJOR_VERSION       0x01
#define SD_MINOR_VERSION       0x00000001

#define SD_TTL                 3

#define SD_ENDPOINT_PORT       30492

#define SD_PROTOCOL_UDP        0x11

/* =========================
 * Session ID
 * ========================= */

static uint16_t Sd_SessionId = 1;

/* =========================
 * 1초 주기 타이머
 * ========================= */

static uint32_t Sd_LastOfferTime = 0;

/* 외부 함수 */
extern uint32_t Get_SystemTime_ms(void);

/* =========================
 * OfferService 송신
 * ========================= */

static void Sd_SendOfferService(void)
{
    uint8_t buffer[64];

    memset(buffer, 0, sizeof(buffer));

    uint8_t* p = buffer;

    /* =====================================================
     * SOME/IP HEADER
     * ===================================================== */

    /* Message ID */
    *((uint16_t*)p) = lwip_htons(0xFFFF);
    p += 2;

    *((uint16_t*)p) = lwip_htons(0x8100);
    p += 2;

    /* Length */
    *((uint32_t*)p) = lwip_htonl(48);
    p += 4;

    /* Request ID */
    *((uint16_t*)p) = lwip_htons(0x0000);
    p += 2;

    *((uint16_t*)p) = lwip_htons(Sd_SessionId++);
    p += 2;

    /* Protocol Version */
    *p++ = 0x01;

    /* Interface Version */
    *p++ = 0x01;

    /* Message Type = Notification */
    *p++ = 0x02;

    /* Return Code */
    *p++ = 0x00;

    /* =====================================================
     * SD HEADER
     * ===================================================== */

    /* Flags */
    *p++ = 0x00;

    /* Reserved */
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x00;

    /* =====================================================
     * Entries Array
     * ===================================================== */

    /* Entries Length */
    *((uint32_t*)p) = lwip_htonl(16);
    p += 4;

    /* Entry Type = OfferService */
    *p++ = 0x01;

    /* Index First Option Run */
    *p++ = 0x00;

    /* Index Second Option Run */
    *p++ = 0x00;

    /* Number of Option 1 / Option 2 */
    *p++ = 0x10;

    /* Service ID */
    *((uint16_t*)p) = lwip_htons(SD_SERVICE_ID);
    p += 2;

    /* Instance ID */
    *((uint16_t*)p) = lwip_htons(SD_INSTANCE_ID);
    p += 2;

    /* Major Version */
    *p++ = SD_MAJOR_VERSION;

    /* TTL (24bit) */
    *p++ = (SD_TTL >> 16) & 0xFF;
    *p++ = (SD_TTL >> 8) & 0xFF;
    *p++ = SD_TTL & 0xFF;

    /* Minor Version */
    *((uint32_t*)p) = lwip_htonl(SD_MINOR_VERSION);
    p += 4;

    /* =====================================================
     * Options Array
     * ===================================================== */

    /* Options Length */
    *((uint32_t*)p) = lwip_htonl(12);
    p += 4;

    /* Option Length */
    *((uint16_t*)p) = lwip_htons(9);
    p += 2;

    /* Option Type = IPv4 Endpoint */
    *p++ = 0x04;

    /* Reserved */
    *p++ = 0x00;

    /* IPv4 Address */
    *p++ = 192;
    *p++ = 168;
    *p++ = 10;
    *p++ = 2;

    /* Reserved */
    *p++ = 0x00;

    /* L4 Proto = UDP */
    *p++ = SD_PROTOCOL_UDP;

    /* Port */
    *((uint16_t*)p) = lwip_htons(SD_ENDPOINT_PORT);
    p += 2;

    /* =====================================================
     * 송신
     * ===================================================== */

    SoAd_TransmitMulticast(
        SD_MULTICAST_IP,
        SD_MULTICAST_PORT,
        buffer,
        (uint16_t)(p - buffer)
    );
}

/* =========================
 * Init
 * ========================= */

void Sd_Init(void)
{
    Sd_SessionId = 1;
    Sd_LastOfferTime = 0;
}

/* =========================
 * MainFunction
 * ========================= */

void Sd_MainFunction(void)
{
    uint32_t now = Get_SystemTime_ms();

    if ((now - Sd_LastOfferTime) >= 1000)
    {
        Sd_LastOfferTime = now;

        Sd_SendOfferService();
    }
}
