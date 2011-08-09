/*
 * Copyright (c) 2001, Swedish Institute of Computer Science.
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: lwipopts.h,v 1.2 2007/09/07 23:28:54 fbernon Exp $
 */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

// for TI C6000 DSP target, struct align....
//------------------------------------------------
#define ETH_PAD_SIZE 					2
//------------------------------------------------

//------------------------------------------------
/* Critical Region Protection */
//------------------------------------------------
/** SYS_LIGHTWEIGHT_PROT
 * define SYS_LIGHTWEIGHT_PROT in lwipopts.h if you want inter-task protection
 * for certain critical regions during buffer allocation, deallocation and memory
 * allocation and deallocation.
 */
#define  SYS_LIGHTWEIGHT_PROT	1 //millin

//----------------------------------------------------------------------
/* ---------- Memory options ---------- */
//----------------------------------------------------------------------

/* MEM_ALIGNMENT: should be set to the alignment of the CPU for which
   lwIP is compiled. 4 byte alignment -> define MEM_ALIGNMENT to 4, 2
   byte alignment -> define MEM_ALIGNMENT to 2. */
#define MEM_ALIGNMENT           4

/* MEM_SIZE: the size of the heap memory. If the application will send
a lot of data that needs to be copied, this should be set high. */
#define MEM_SIZE                65536

/* MEMP_NUM_PBUF: the number of memp struct pbufs. If the application
   sends a lot of data out of ROM (or other static memory), this
   should be set high. */
#define MEMP_NUM_PBUF           32

/* Number of raw connection PCBs */
#define MEMP_NUM_RAW_PCB        8

/* MEMP_NUM_UDP_PCB: the number of UDP protocol control blocks. One
   per active UDP "connection". */
#define MEMP_NUM_UDP_PCB        8

/* MEMP_NUM_TCP_PCB: the number of simulatenously active TCP
   connections. */
#define MEMP_NUM_TCP_PCB        8

/* MEMP_NUM_TCP_PCB_LISTEN: the number of listening TCP
   connections. */
#define MEMP_NUM_TCP_PCB_LISTEN 2

/* MEMP_NUM_TCP_SEG: the number of simultaneously queued TCP
   segments. */
#define MEMP_NUM_TCP_SEG        32

/* MEMP_NUM_SYS_TIMEOUT: the number of simulateously active
   timeouts. */
#define MEMP_NUM_SYS_TIMEOUT    8


//----------------------------------------------------------------------
/* The following four are used only with the sequential API and can be
   set to 0 if the application only will use the raw API. */
//----------------------------------------------------------------------
/* MEMP_NUM_NETBUF: the number of struct netbufs. */
#define MEMP_NUM_NETBUF         32

/* MEMP_NUM_NETCONN: the number of struct netconns. */
#define MEMP_NUM_NETCONN        32

/* MEMP_NUM_TCPIP_MSG: the number of struct tcpip_msg, which is used
   for sequential API communication and incoming packets. Used in
   src/api/tcpip.c. */
#define MEMP_NUM_TCPIP_MSG_API   32
#define MEMP_NUM_TCPIP_MSG_INPKT 32

//----------------------------------------------------------------------
/* ---------- Pbuf options ---------- */
//----------------------------------------------------------------------
/* PBUF_POOL_SIZE: the number of buffers in the pbuf pool. */
#define PBUF_POOL_SIZE          16

/* PBUF_POOL_BUFSIZE: the size of each pbuf in the pbuf pool. */
#define PBUF_POOL_BUFSIZE       1536



//----------------------------------------------------------------------
/* ---------- ARP options ---------- */
//----------------------------------------------------------------------
/** Number of active hardware address, IP address pairs cached */
#define ARP_TABLE_SIZE                  10//10
/**
 * If enabled, outgoing packets are queued during hardware address
 * resolution.
 *
 * This feature has not stabilized yet. Single-packet queueing is
 * believed to be stable, multi-packet queueing is believed to
 * clash with the TCP segment queueing.
 * 
 * As multi-packet-queueing is currently disabled, enabling this
 * _should_ work, but we need your testing feedback on lwip-users.
 *
 */
#define ARP_QUEUEING                    0
// if TCP was used, must disable this in v1.1.0 //millin


//----------------------------------------------------------------------
/* ---------- IP options ---------- */
//----------------------------------------------------------------------
/* Define IP_FORWARD to 1 if you wish to have the ability to forward
   IP packets across network interfaces. If you are going to run lwIP
   on a device with only one network interface, define this to 0. */
#define IP_FORWARD              0

/* If defined to 1, IP options are allowed (but not parsed). If
   defined to 0, all packets with IP options are dropped. */
#define IP_OPTIONS              0

/** IP reassembly and segmentation. Even if they both deal with IP
 *  fragments, note that these are orthogonal, one dealing with incoming
 *  packets, the other with outgoing packets
 */

/** Reassemble incoming fragmented IP packets */
#define IP_REASSEMBLY                   0  

/** Fragment outgoing IP packets if their size exceeds MTU */
#define IP_FRAG                         0  


//----------------------------------------------------------------------
/* ---------- DHCP options ---------- */
//----------------------------------------------------------------------

/* Define LWIP_DHCP to 1 if you want DHCP configuration of
   interfaces. DHCP is not implemented in lwIP 0.5.1, however, so
   turning this on does currently not work. */
#define LWIP_DHCP               0

/* 1 if you want to do an ARP check on the offered address
   (recommended). */
#define DHCP_DOES_ARP_CHECK     1


//----------------------------------------------------------------------
/* ---------- UDP options ---------- */
//----------------------------------------------------------------------
#define LWIP_UDP                1


//----------------------------------------------------------------------
/* ---------- TCP options ---------- */
//----------------------------------------------------------------------
#define LWIP_TCP                1

/* TCP receive window. */
#define TCP_WND                 32768

/* Maximum number of retransmissions of data segments. */
#define TCP_MAXRTX              4

/* Maximum number of retransmissions of SYN segments. */
#define TCP_SYNMAXRTX           4

/* Controls if TCP should queue segments that arrive out of
   order. Define to 0 if your device is low on memory. */
#define TCP_QUEUE_OOSEQ         1

/* TCP Maximum segment size. */
#define TCP_MSS                 1476

/* TCP sender buffer space (bytes). */
#define TCP_SND_BUF             32768


//----------------------------------------------------------------------
/* ---------- Other options ---------- */
//----------------------------------------------------------------------

/* Support loop interface (127.0.0.1) */
#define LWIP_HAVE_LOOPIF				0

#define LWIP_COMPAT_SOCKETS             1

// for uC/OS-II port on TI DSP
#define TCPIP_THREAD_PRIO               5 //millin
//#define SLIPIF_THREAD_PRIO              1
//#define PPP_THREAD_PRIO                 1


//----------------------------------------------------------------------
/* ---------- Socket Options ---------- */
//----------------------------------------------------------------------

/* Enable SO_REUSEADDR and SO_REUSEPORT options */ 
#define SO_REUSE 0


//----------------------------------------------------------------------
/* ---------- Statistics options ---------- */
//----------------------------------------------------------------------

#define STATS		0

#if LWIP_STATS

#define LWIP_STATS_DISPLAY	1
#define LINK_STATS			1
#define IP_STATS			1
#define IPFRAG_STATS		1
#define ICMP_STATS			1
#define UDP_STATS			1
#define TCP_STATS			1
#define MEM_STATS			1
#define MEMP_STATS			1
#define PBUF_STATS			1
#define SYS_STATS			1
#endif /* STATS */


//----------------------------------------------------------------------
/* ------------if you need to do debug-------------*/
//----------------------------------------------------------------------
/*
define LWIP_DEBUG in compiler    and following...
*/
#define LWIP_DBG_MIN_LEVEL					LWIP_DBG_LEVEL_SERIOUS
//LWIP_DBG_LEVEL_WARNING LWIP_DBG_LEVEL_SERIOUS LWIP_DBG_LEVEL_SEVERE

#define LWIP_DBG_TYPES_ON					0//LWIP_DBG_TRACE | LWIP_DBG_STATE |LWIP_DBG_FRESH | LWIP_DBG_HALT
/*
Then, define debug class in opt.h
* --------------------------------------------------*/

#define ETHARP_DEBUG                    LWIP_DBG_OFF
#define NETIF_DEBUG                     LWIP_DBG_OFF
#define PBUF_DEBUG                      LWIP_DBG_OFF
#define API_LIB_DEBUG                   LWIP_DBG_OFF
#define API_MSG_DEBUG                   LWIP_DBG_OFF
#define SOCKETS_DEBUG                   LWIP_DBG_OFF
#define ICMP_DEBUG                      LWIP_DBG_OFF
#define INET_DEBUG                      LWIP_DBG_OFF
#define IP_DEBUG                        LWIP_DBG_OFF
#define IP_REASS_DEBUG                  LWIP_DBG_OFF
#define RAW_DEBUG                       LWIP_DBG_OFF
#define MEM_DEBUG                       LWIP_DBG_OFF
#define MEMP_DEBUG                      LWIP_DBG_OFF
#define SYS_DEBUG                       LWIP_DBG_OFF
#define TCP_DEBUG                       LWIP_DBG_OFF
#define TCP_INPUT_DEBUG                 LWIP_DBG_OFF
#define TCP_FR_DEBUG                    LWIP_DBG_OFF
#define TCP_RTO_DEBUG                   LWIP_DBG_OFF
#define TCP_CWND_DEBUG                  LWIP_DBG_OFF
#define TCP_WND_DEBUG                   LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG                LWIP_DBG_OFF
#define TCP_RST_DEBUG                   LWIP_DBG_OFF
#define TCP_QLEN_DEBUG                  LWIP_DBG_OFF
#define UDP_DEBUG                       LWIP_DBG_OFF
#define TCPIP_DEBUG                     LWIP_DBG_OFF
#define PPP_DEBUG                       LWIP_DBG_OFF
#define SLIP_DEBUG                      LWIP_DBG_OFF
#define DHCP_DEBUG                      LWIP_DBG_OFF


#endif /* __LWIPOPTS_H__ */
