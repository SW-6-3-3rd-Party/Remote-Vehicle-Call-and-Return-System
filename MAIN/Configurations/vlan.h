#ifndef VLAN_H
#define VLAN_H

#include "lwip/netif.h"
#include "lwip/prot/ethernet.h"
#include "lwip/pbuf.h"

int my_vlan_check(struct netif *netif,
                  struct eth_hdr *eth_hdr,
                  struct eth_vlan_hdr *vlan_hdr);

s32_t my_vlan_set(struct netif *netif,
                  struct pbuf *p,
                  const struct eth_addr *src,
                  const struct eth_addr *dst,
                  u16_t eth_type);

#endif
