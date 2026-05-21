#include "lwip/netif.h"
#include "lwip/prot/ethernet.h"
#include "lwip/pbuf.h"

#define MY_VLAN_ID 10

s32_t my_vlan_set(struct netif *netif,
                  struct pbuf *p,
                  const struct eth_addr *src,
                  const struct eth_addr *dst,
                  u16_t eth_type)
{
    LWIP_UNUSED_ARG(netif);
    LWIP_UNUSED_ARG(p);
    LWIP_UNUSED_ARG(src);
    LWIP_UNUSED_ARG(dst);
    LWIP_UNUSED_ARG(eth_type);

    return MY_VLAN_ID;
}

int my_vlan_check(struct netif *netif,
                  struct eth_hdr *eth_hdr,
                  struct eth_vlan_hdr *vlan_hdr)
{
    u16_t vlan = lwip_htons(vlan_hdr->prio_vid) & 0x0FFF;

    if(vlan == MY_VLAN_ID)
    {
        return 1;
    }

    return 0;
}

