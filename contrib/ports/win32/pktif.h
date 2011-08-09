#ifndef __PKTIF_H__
#define __PKTIF_H__

#include "lwip/err.h"
#include "lwip/netif.h"

err_t ethernetif_init    (struct netif *netif);
void  ethernetif_shutdown(struct netif *netif);
void  ethernetif_poll    (struct netif *netif);

#endif
