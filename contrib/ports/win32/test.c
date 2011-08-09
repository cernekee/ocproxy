/*
 * Copyright (c) 2001,2002 Florian Schulze.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the authors nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * test.c - This file is part of lwIP test
 *
 */

/* C runtime includes */
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <conio.h>

/* lwIP core includes */
#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/timers.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"

/* lwIP netif includes */
#include "netif/etharp.h"

/* applications includes */
#include "apps/httpserver_raw/httpd.h"
#include "apps/httpserver/httpserver-netconn.h"
#include "apps/netio/netio.h"
#include "apps/netbios/netbios.h"
#include "apps/ping/ping.h"
#include "apps/rtp/rtp.h"
#include "apps/sntp/sntp.h"
#include "apps/chargen/chargen.h"
#include "apps/shell/shell.h"
#include "apps/tcpecho/tcpecho.h"
#include "apps/udpecho/udpecho.h"
#include "apps/tcpecho_raw/echo.h"
#include "apps/socket_examples/socket_examples.h"

#if NO_SYS
/* ... then we need information about the timer intervals: */
#include "lwip/ip_frag.h"
#include "lwip/igmp.h"
#endif /* NO_SYS */

#if PPP_SUPPORT
/* PPP includes */
#include "../netif/ppp/ppp.h"
#include "../netif/ppp/lcp.h"
#include "lwip/sio.h"
#include "netif/ppp_oe.h"
#endif /* PPP_SUPPORT */

#include "pcapif.h"

/* include the port-dependent configuration */
#include "lwipcfg_msvc.h"

/** Use an ethernet adapter? By default only if PPP is not used. */
#ifndef USE_ETHERNET
#define USE_ETHERNET  (!PPP_SUPPORT || PPPOE_SUPPORT)
#endif
/** Use an ethernet adapter for TCP/IP? By default only if PPP is not used. */
#ifndef USE_ETHERNET_TCPIP
#define USE_ETHERNET_TCPIP  !PPP_SUPPORT
#endif


/* globales variables for netifs */
#if USE_ETHERNET
/* THE ethernet interface */
struct netif netif;
#if LWIP_DHCP
/* dhcp struct for the ethernet netif */
struct dhcp netif_dhcp;
#endif /* LWIP_DHCP */
#if LWIP_AUTOIP
/* autoip struct for the ethernet netif */
struct autoip netif_autoip;
#endif /* LWIP_AUTOIP */
#endif /* USE_ETHERNET */
#if PPP_SUPPORT
/* THE PPP descriptor */
int ppp_desc = -1;
u8_t sio_idx = 0;
sio_fd_t ppp_sio;
#endif /* PPP_SUPPORT */


#if PPP_SUPPORT
void
pppLinkStatusCallback(void *ctx, int errCode, void *arg)
{
  LWIP_UNUSED_ARG(ctx);

  switch(errCode) {
    case PPPERR_NONE: {             /* No error. */
      struct ppp_addrs *ppp_addrs = arg;

      printf("pppLinkStatusCallback: PPPERR_NONE\n");
      printf(" our_ipaddr=%s\n", ip_ntoa(&ppp_addrs->our_ipaddr));
      printf(" his_ipaddr=%s\n", ip_ntoa(&ppp_addrs->his_ipaddr));
      printf(" netmask   =%s\n", ip_ntoa(&ppp_addrs->netmask));
      printf(" dns1      =%s\n", ip_ntoa(&ppp_addrs->dns1));
      printf(" dns2      =%s\n", ip_ntoa(&ppp_addrs->dns2));
      break;
    }
    case PPPERR_PARAM: {           /* Invalid parameter. */
      printf("pppLinkStatusCallback: PPPERR_PARAM\n");
      break;
    }
    case PPPERR_OPEN: {            /* Unable to open PPP session. */
      printf("pppLinkStatusCallback: PPPERR_OPEN\n");
      break;
    }
    case PPPERR_DEVICE: {          /* Invalid I/O device for PPP. */
      printf("pppLinkStatusCallback: PPPERR_DEVICE\n");
      break;
    }
    case PPPERR_ALLOC: {           /* Unable to allocate resources. */
      printf("pppLinkStatusCallback: PPPERR_ALLOC\n");
      break;
    }
    case PPPERR_USER: {            /* User interrupt. */
      printf("pppLinkStatusCallback: PPPERR_USER\n");
      break;
    }
    case PPPERR_CONNECT: {         /* Connection lost. */
      printf("pppLinkStatusCallback: PPPERR_CONNECT\n");
      break;
    }
    case PPPERR_AUTHFAIL: {        /* Failed authentication challenge. */
      printf("pppLinkStatusCallback: PPPERR_AUTHFAIL\n");
      break;
    }
    case PPPERR_PROTOCOL: {        /* Failed to meet protocol. */
      printf("pppLinkStatusCallback: PPPERR_PROTOCOL\n");
      break;
    }
    default: {
      printf("pppLinkStatusCallback: unknown errCode %d\n", errCode);
      break;
    }
  }
}
#endif /* PPP_SUPPORT */

#if LWIP_NETIF_STATUS_CALLBACK
void status_callback(struct netif *netif)
{
  if (netif_is_up(netif)) {
    printf("status_callback==UP, local interface IP is %s\n", ip_ntoa(&netif->ip_addr));
  } else {
    printf("status_callback==DOWN\n");
  }
}
#endif /* LWIP_NETIF_STATUS_CALLBACK */

#if LWIP_NETIF_LINK_CALLBACK
void link_callback(struct netif *netif)
{
  if (netif_is_link_up(netif)) {
    printf("link_callback==UP\n");
#if LWIP_DHCP
    if (netif->dhcp != NULL) {
      dhcp_renew(netif);
    }
#endif /* LWIP_DHCP */
  } else {
    printf("link_callback==DOWN\n");
  }
}
#endif /* LWIP_NETIF_LINK_CALLBACK */

/* This function initializes all network interfaces */
static void
msvc_netif_init()
{
#if USE_ETHERNET
  ip_addr_t ipaddr, netmask, gw;
#endif /* USE_ETHERNET */

#if PPP_SUPPORT
  const char *username = NULL, *password = NULL;
#ifdef PPP_USERNAME
  username = PPP_USERNAME;
#endif
#ifdef PPP_PASSWORD
  password = PPP_PASSWORD;
#endif
  printf("pppInit\n");
  pppInit();
  pppSetAuth(PPPAUTHTYPE_ANY, username, password);
  printf("pppOpen: COM%d\n", (int)sio_idx);
#if PPPOS_SUPPORT
  ppp_sio = sio_open(sio_idx);
  if (ppp_sio == NULL) {
    printf("sio_open error\n");
  } else {
    ppp_desc = pppOpen(ppp_sio, pppLinkStatusCallback, NULL);
  }
#endif /* PPPOS_SUPPORT */
#endif  /* PPP_SUPPORT */

#if USE_ETHERNET
  ip_addr_set_zero(&gw);
  ip_addr_set_zero(&ipaddr);
  ip_addr_set_zero(&netmask);
#if USE_ETHERNET_TCPIP
#if LWIP_DHCP
  printf("Starting lwIP, local interface IP is dhcp-enabled\n");
#elif LWIP_AUTOIP
  printf("Starting lwIP, local interface IP is autoip-enabled\n");
#else /* LWIP_AUTOIP */
  LWIP_PORT_INIT_GW(&gw);
  LWIP_PORT_INIT_IPADDR(&ipaddr);
  LWIP_PORT_INIT_NETMASK(&netmask);
  printf("Starting lwIP, local interface IP is %s\n", ip_ntoa(&ipaddr));
#endif /* LWIP_DHCP */
#endif /* USE_ETHERNET_TCPIP */

#if NO_SYS
#if LWIP_ARP
  netif_set_default(netif_add(&netif, &ipaddr, &netmask, &gw, NULL, pcapif_init, ethernet_input));
#else /* LWIP_ARP */
  netif_set_default(netif_add(&netif, &ipaddr, &netmask, &gw, NULL, pcapif_init, ip_input));
#endif /* LWIP_ARP */
#else  /* NO_SYS */
  netif_set_default(netif_add(&netif, &ipaddr, &netmask, &gw, NULL, pcapif_init, tcpip_input));
#endif /* NO_SYS */
#if LWIP_NETIF_STATUS_CALLBACK
  netif_set_status_callback(&netif, status_callback);
#endif /* LWIP_NETIF_STATUS_CALLBACK */
#if LWIP_NETIF_LINK_CALLBACK
  netif_set_link_callback(&netif, link_callback);
#endif /* LWIP_NETIF_LINK_CALLBACK */

#if USE_ETHERNET_TCPIP
#if LWIP_AUTOIP
  autoip_set_struct(&netif, &netif_autoip);
#endif /* LWIP_AUTOIP */
#if LWIP_DHCP
  dhcp_set_struct(&netif, &netif_dhcp);
  dhcp_start(&netif);
#elif LWIP_AUTOIP
  autoip_start(&netif);
#else /* LWIP_DHCP */
  netif_set_up(&netif);
#endif /* LWIP_DHCP */
#else /* USE_ETHERNET_TCPIP */
  /* Use ethernet for PPPoE only */
  netif.flags &= ~(NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP); /* no ARP */
  netif.flags |= NETIF_FLAG_ETHERNET; /* but pure ethernet */
#endif /* USE_ETHERNET_TCPIP */

#if PPP_SUPPORT && PPPOE_SUPPORT
  /* start PPPoE after ethernet netif is added! */
  ppp_desc = pppOverEthernetOpen(&netif, NULL, NULL, pppLinkStatusCallback, NULL);
#endif /* PPP_SUPPORT && PPPOE_SUPPORT */

#endif /* USE_ETHERNET */
}

#if LWIP_DNS_APP && LWIP_DNS
void dns_found(const char *name, ip_addr_t *addr, void *arg)
{
  LWIP_UNUSED_ARG(arg);
  printf("%s: %s\n", name, addr ? ip_ntoa(addr) : "<not found>");
}

void dns_dorequest(void *arg)
{
  char* dnsname = "3com.com";
  ip_addr_t dnsresp;
  LWIP_UNUSED_ARG(arg);
 
  if (dns_gethostbyname(dnsname, &dnsresp, dns_found, 0) == ERR_OK) {
    dns_found(dnsname, &dnsresp, 0);
  }
}
#endif /* LWIP_DNS_APP && LWIP_DNS */

/* This function initializes applications */
static void
apps_init()
{
#if LWIP_DNS_APP && LWIP_DNS
  /* wait until the netif is up (for dhcp, autoip or ppp) */
  sys_timeout(5000, dns_dorequest, NULL);
#endif /* LWIP_DNS_APP && LWIP_DNS */

#if LWIP_CHARGEN_APP && LWIP_SOCKET
  chargen_init();
#endif /* LWIP_CHARGEN_APP && LWIP_SOCKET */

#if LWIP_PING_APP && LWIP_RAW && LWIP_ICMP
  ping_init();
#endif /* LWIP_PING_APP && LWIP_RAW && LWIP_ICMP */

#if LWIP_NETBIOS_APP && LWIP_UDP
  netbios_init();
#endif /* LWIP_NETBIOS_APP && LWIP_UDP */

#if LWIP_HTTPD_APP && LWIP_TCP
#ifdef LWIP_HTTPD_APP_NETCONN
  http_server_netconn_init();
#else /* LWIP_HTTPD_APP_NETCONN */
  httpd_init();
#endif /* LWIP_HTTPD_APP_NETCONN */
#endif /* LWIP_HTTPD_APP && LWIP_TCP */

#if LWIP_NETIO_APP && LWIP_TCP
  netio_init();
#endif /* LWIP_NETIO_APP && LWIP_TCP */

#if LWIP_RTP_APP && LWIP_SOCKET && LWIP_IGMP
  rtp_init();
#endif /* LWIP_RTP_APP && LWIP_SOCKET && LWIP_IGMP */

#if LWIP_SNTP_APP && LWIP_SOCKET
  sntp_init();
#endif /* LWIP_SNTP_APP && LWIP_SOCKET */

#if LWIP_SHELL_APP && LWIP_NETCONN
  shell_init();
#endif /* LWIP_SHELL_APP && LWIP_NETCONN */
#if LWIP_TCPECHO_APP
#if LWIP_NETCONN && defined(LWIP_TCPECHO_APP_NETCONN)
  tcpecho_init();
#else /* LWIP_NETCONN && defined(LWIP_TCPECHO_APP_NETCONN) */
  echo_init();
#endif
#endif /* LWIP_TCPECHO_APP && LWIP_NETCONN */
#if LWIP_UDPECHO_APP && LWIP_NETCONN
  udpecho_init();
#endif /* LWIP_UDPECHO_APP && LWIP_NETCONN */
#if LWIP_SOCKET_EXAMPLES_APP && LWIP_SOCKET
  socket_examples_init();
#endif /* LWIP_SOCKET_EXAMPLES_APP && LWIP_SOCKET */
}

/* This function initializes this lwIP test. When NO_SYS=1, this is done in
 * the main_loop context (there is no other one), when NO_SYS=0, this is done
 * in the tcpip_thread context */
static void
test_init(void * arg)
{ /* remove compiler warning */
#if NO_SYS
  LWIP_UNUSED_ARG(arg);
#else /* NO_SYS */
  sys_sem_t *init_sem;
  LWIP_ASSERT("arg != NULL", arg != NULL);
  init_sem = (sys_sem_t*)arg;
#endif /* NO_SYS */

  /* init network interfaces */
  msvc_netif_init();

  /* init apps */
  apps_init();

#if !NO_SYS
  sys_sem_signal(init_sem);
#endif /* !NO_SYS */
}

#if PPP_SUPPORT
static void pppCloseCallback(void *arg)
{
  int pd = (int)arg;
  pppClose(pd);
}
#endif /* PPP_SUPPORT */

/* This is somewhat different to other ports: we have a main loop here:
 * a dedicated task that waits for packets to arrive. This would normally be
 * done from interrupt context with embedded hardware, but we don't get an
 * interrupt in windows for that :-) */
void main_loop()
{
#if !NO_SYS
  err_t err;
  sys_sem_t init_sem;
#endif /* NO_SYS */
#if PPP_SUPPORT
#if !USE_ETHERNET
  int count;
  u8_t rxbuf[1024];
#endif
  volatile int callClosePpp = 0;
#endif /* PPP_SUPPORT */

  /* initialize lwIP stack, network interfaces and applications */
#if NO_SYS
  lwip_init();
  test_init(NULL);
#else /* NO_SYS */
  err = sys_sem_new(&init_sem, 0);
  tcpip_init(test_init, &init_sem);
  /* we have to wait for initialization to finish before
   * calling update_adapter()! */
  sys_sem_wait(&init_sem);
  sys_sem_free(&init_sem);
#endif /* NO_SYS */

  /* MAIN LOOP for driver update (and timers if NO_SYS) */
  while (!_kbhit()) {
#if NO_SYS
    /* handle timers (already done in tcpip.c when NO_SYS=0) */
    sys_check_timeouts();
#endif /* NO_SYS */

#if USE_ETHERNET
#if !PCAPIF_RX_USE_THREAD
    /* check for packets and link status*/
    pcapif_poll(&netif);
    /* When pcapif_poll comes back, there are not packets, so sleep to
       prevent 100% CPU load. Don't do this in an embedded system since it
       increases latency! */
    sys_msleep(1);
#else /* !PCAPIF_RX_USE_THREAD */
    sys_msleep(50);
#endif /* !PCAPIF_RX_USE_THREAD */
#else /* USE_ETHERNET */
#if 0 /* set this to 1 if PPP_INPROC_OWNTHREAD==0 or not defined (see ppp.c) */
    /* try to read characters from serial line and pass them to PPPoS */
    count = sio_read(ppp_sio, (u8_t*)rxbuf, 1024);
    if(count > 0) {
      pppos_input(ppp_desc, rxbuf, count);
    } else
#endif
    {
      /* nothing received, give other tasks a chance to run */
      sys_msleep(1);
    }

#endif /* USE_ETHERNET */
#if !LWIP_NETIF_LOOPBACK_MULTITHREADING
    /* check for loopback packets on all netifs */
    netif_poll_all();
#endif /* !LWIP_NETIF_LOOPBACK_MULTITHREADING */
#if PPP_SUPPORT
    {
    int do_hup = 0;
    if(do_hup) {
      pppSigHUP(ppp_desc);
      do_hup = 0;
    }
    }
    if(callClosePpp && (ppp_desc >= 0)) {
      /* make sure to disconnect PPP before stopping the program... */
      callClosePpp = 0;
#if NO_SYS
      pppClose(ppp_desc);
#else
      tcpip_callback_with_block(pppCloseCallback, (void*)ppp_desc, 0);
#endif
      ppp_desc = -1;
    }
#endif /* PPP_SUPPORT */
  }

#if PPP_SUPPORT
    if(ppp_desc >= 0) {
      u32_t started;
      printf("Closing PPP connection...\n");
      /* make sure to disconnect PPP before stopping the program... */
#if NO_SYS
      pppClose(ppp_desc);
#else
      tcpip_callback_with_block(pppCloseCallback, (void*)ppp_desc, 0);
#endif
      ppp_desc = -1;
      /* Wait for some time to let PPP finish... */
      started = sys_now();
      do
      {
#if USE_ETHERNET
        pcapif_poll(&netif);
#else /* USE_ETHERNET */
        sys_msleep(50);
#endif /* USE_ETHERNET */
        /* @todo: need a better check here: only wait until PPP is down */
      } while(sys_now() - started < 5000);
    }
#endif /* PPP_SUPPORT */
#if USE_ETHERNET
  /* release the pcap library... */
  pcapif_shutdown(&netif);
#endif /* USE_ETHERNET */
}

#if PPP_SUPPORT && PPPOS_SUPPORT
int main(int argc, char **argv)
#else /* PPP_SUPPORT && PPPOS_SUPPORT */
int main(void)
#endif /* PPP_SUPPORT && PPPOS_SUPPORT */
{
#if PPP_SUPPORT && PPPOS_SUPPORT
  if(argc > 1) {
    sio_idx = (u8_t)atoi(argv[1]);
  }
  printf("Using serial port %d for PPP\n", sio_idx);
#endif /* PPP_SUPPORT && PPPOS_SUPPORT */
  /* no stdio-buffering, please! */
  setvbuf(stdout, NULL,_IONBF, 0);

  main_loop();

  return 0;
}
