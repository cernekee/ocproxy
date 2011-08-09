/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 *
 * Copyright (c) 2001, 2002, 2003 Xilinx, Inc.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO 
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS".
 * BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS ONE POSSIBLE 
 * IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD, XILINX 
 * IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE FROM 
 * ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING 
 * ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.  XILINX 
 * EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO THE 
 * ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY 
 * WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE 
 * FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY 
 * AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Chris Borrelli <chris.borrelli@xilinx.com>
 * 
 * Based on example ethernetif.c, Adam Dunkels <adam@sics.se>
 *
 */

/*---------------------------------------------------------------------------*/
/* EDK Include Files                                                         */
/*---------------------------------------------------------------------------*/
#include "xemac.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xexception_l.h"

/*---------------------------------------------------------------------------*/
/* LWIP Include Files                                                        */
/*---------------------------------------------------------------------------*/
#include "lwip/debug.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/sys.h"
#include "lwip/netif.h"
#include "netif/etharp.h"
#include "netif/xemacif.h"

/*---------------------------------------------------------------------------*/
/* Describe network interface                                                */
/*---------------------------------------------------------------------------*/
#define IFNAME0 'e'
#define IFNAME1 '0'

/*---------------------------------------------------------------------------*/
/* Constant Definitions                                                      */
/*---------------------------------------------------------------------------*/
#define XEM_MAX_FRAME_SIZE_IN_WORDS ((XEM_MAX_FRAME_SIZE/sizeof(Xuint32))+1)

extern XEmacIf_Config XEmacIf_ConfigTable[];

/*---------------------------------------------------------------------------*/
/* low_level_init function                                                   */
/*    - hooks up the data structures and sets the mac options and mac        */
/*---------------------------------------------------------------------------*/
static err_t 
low_level_init(struct netif *netif_ptr)
{
   XEmac *InstancePtr = mem_malloc(sizeof(XEmac));
   XEmacIf_Config *xemacif_ptr = (XEmacIf_Config *) netif_ptr->state;
   Xuint16 DeviceId = xemacif_ptr->DevId;
   XStatus Result;
   Xuint32 Options;

   xemacif_ptr->instance_ptr = InstancePtr;

   /* Call Initialize Function of EMAC driver */
   Result = XEmac_Initialize(InstancePtr, DeviceId);
   if (Result != XST_SUCCESS) return ERR_MEM;

   /* Stop the EMAC hardware */
   XEmac_Stop(InstancePtr);

   /* Set MAC Address of EMAC */
   Result = XEmac_SetMacAddress(InstancePtr, (Xuint8*) netif_ptr->hwaddr);
   if (Result != XST_SUCCESS) return ERR_MEM;

   /* Set MAC Options */
   
   Options = ( XEM_INSERT_FCS_OPTION | 
               XEM_INSERT_PAD_OPTION | 
               XEM_UNICAST_OPTION | 
               XEM_BROADCAST_OPTION | 
               XEM_POLLED_OPTION |
               XEM_STRIP_PAD_FCS_OPTION);

   Result = XEmac_SetOptions(InstancePtr, Options);
   if (Result != XST_SUCCESS) return ERR_MEM;

   /* Start the EMAC hardware */
   Result = XEmac_Start(InstancePtr);
   if (Result != XST_SUCCESS) return ERR_MEM;

   /* Clear driver stats */
   XEmac_ClearStats(InstancePtr);
   
   return ERR_OK;
}

/*---------------------------------------------------------------------------*/
/* low_level_output()                                                        */
/*                                                                           */
/* Should do the actual transmission of the packet. The packet is            */
/* contained in the pbuf that is passed to the function. This pbuf           */
/* might be chained.                                                         */
/*---------------------------------------------------------------------------*/
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
   struct pbuf *q;
   u32_t frame_buffer[XEM_MAX_FRAME_SIZE_IN_WORDS];  /* word aligned */
   Xuint8 *frame_ptr;
   int payload_size = 0, i;
   XStatus Result;
   Xuint32 Options;
   XEmacIf_Config *xemacif_ptr = netif->state;

   frame_ptr = (Xuint8 *) frame_buffer;

   for(q = p; q != NULL; q = q->next) {
      /*
       * Send the data from the pbuf to the interface, one pbuf at a
       * time. The size of the data in each pbuf is kept in the ->len
       * variable.
       */
      for(i = 0 ; i < q->len ; i++) {
         *(frame_ptr++) = (Xuint8) *(((u8_t *) q->payload) + i);
         payload_size++;
      }
   }

   Result = XEmac_PollSend(xemacif_ptr->instance_ptr,
                           (Xuint8 *) frame_buffer,
                           payload_size);

   if (Result != XST_SUCCESS)
   {
      xil_printf("XEmac_PollSend: failed\r\n");
      if (Result == XST_FIFO_ERROR)
      {
         XEmac_Reset(xemacif_ptr->instance_ptr);
         XEmac_SetMacAddress(xemacif_ptr->instance_ptr, 
               (Xuint8*) xemacif_ptr->ethaddr.addr);
         Options = ( XEM_INSERT_FCS_OPTION | 
                     XEM_INSERT_PAD_OPTION | 
                     XEM_UNICAST_OPTION | 
                     XEM_BROADCAST_OPTION | 
                     XEM_POLLED_OPTION | 
                     XEM_STRIP_PAD_FCS_OPTION);
         XEmac_SetOptions(xemacif_ptr->instance_ptr, Options);
         XEmac_Start(xemacif_ptr->instance_ptr);
         xil_printf("XEmac_PollSend: returned XST_FIFO_ERROR\r\n");
      }
      return ERR_MEM;
   }

#if 0
   xil_printf("\r\n\r\n TXFRAME:\r\n");
   for (i=0 ; i < payload_size ; i++) {
      xil_printf("%2X", ((Xuint8 *) frame_buffer)[i]);
      if (! (i%20) && i) xil_printf("\r\n");
      else xil_printf(" ");
   }
   xil_printf ("\r\n\r\n");
#endif

#ifdef LINK_STATS
   lwip_stats.link.xmit++;
#endif /* LINK_STATS */

   return ERR_OK;
}

/*---------------------------------------------------------------------------*/
/* low_level_input()                                                         */
/*                                                                           */
/* Allocates a pbuf pool and transfers bytes of                              */
/* incoming packet from the interface into the pbuf.                         */
/*---------------------------------------------------------------------------*/
static struct pbuf * low_level_input(XEmacIf_Config *xemacif_ptr)
{
   struct pbuf *p = NULL, *q = NULL;
   XEmac *EmacPtr = (XEmac *) xemacif_ptr->instance_ptr;
   
   Xuint32 RecvBuffer[XEM_MAX_FRAME_SIZE_IN_WORDS];
   Xuint32 FrameLen = XEM_MAX_FRAME_SIZE;
   Xuint32 i, Options;
   u8_t * frame_bytes = (u8_t *) RecvBuffer;
   XStatus Result;

   Result = XEmac_PollRecv(EmacPtr, (Xuint8 *)RecvBuffer, &FrameLen);

   if (Result != XST_SUCCESS)
   {
      if (!(Result == XST_NO_DATA || Result == XST_BUFFER_TOO_SMALL))
      {
         XEmac_Reset(xemacif_ptr->instance_ptr);
         XEmac_SetMacAddress(xemacif_ptr->instance_ptr, 
               (Xuint8*) xemacif_ptr->ethaddr.addr);
         Options = ( XEM_INSERT_FCS_OPTION | 
                     XEM_INSERT_PAD_OPTION | 
                     XEM_UNICAST_OPTION | 
                     XEM_BROADCAST_OPTION | 
                     XEM_POLLED_OPTION | 
                     XEM_STRIP_PAD_FCS_OPTION);
         XEmac_SetOptions(xemacif_ptr->instance_ptr, Options);
         XEmac_Start(xemacif_ptr->instance_ptr);
      }
      return p;
   }

#if 0
   xil_printf("\r\n");
   for (i=0 ; i < FrameLen ; i++) {
      xil_printf("%2X", frame_bytes[i]);
      if (! (i%20) && i) xil_printf("\r\n");
      else xil_printf(" ");
   }
   xil_printf ("\r\n");
#endif

   /* Allocate a pbuf chain of pbufs from the pool. */
   p = pbuf_alloc(PBUF_RAW, FrameLen, PBUF_POOL);

   if (p != NULL) {
   /* Iterate over the pbuf chain until we have
    * read the entire packet into the pbuf. */
      for(q = p; q != NULL; q = q->next) {
         /* Read enough bytes to fill this pbuf 
          * in the chain.  The available data in 
          * the pbuf is given by the q->len variable. */
         for (i = 0 ; i < q->len ; i++) {
            ((u8_t *)q->payload)[i] = *(frame_bytes++);
         }
      }

#ifdef LINK_STATS
      lwip_stats.link.recv++;
#endif /* LINK_STATS */      

   } else {

#ifdef LINK_STATS
      lwip_stats.link.memerr++;
      lwip_stats.link.drop++;
#endif /* LINK_STATS */ 
      ;
   }
   return p;  
}

/*---------------------------------------------------------------------------*/
/* xemacif_input():                                                          */
/*                                                                           */
/* This function should be called when a packet is ready to be read          */
/* from the interface. It uses the function low_level_input() that           */
/* should handle the actual reception of bytes from the network              */
/* interface.                                                                */
/*---------------------------------------------------------------------------*/
err_t xemacif_input(void *CallBackRef)
{
   struct netif * netif_ptr = (struct netif *) CallBackRef;
   XEmacIf_Config * xemacif_ptr;
   struct eth_hdr * ethernet_header;
   struct pbuf *p;

   xemacif_ptr = netif_ptr->state;

   p = low_level_input(xemacif_ptr);

   if (p != NULL) {
      ethernet_header = p->payload;

      q = NULL;
      switch (htons(ethernet_header->type)) {
      case ETHTYPE_IP:
         etharp_ip_input(netif_ptr, p);
         pbuf_header(p, -14);
         netif_ptr->input(p, netif_ptr);
         break;
      case ETHTYPE_ARP:
         etharp_arp_input(netif_ptr, &(xemacif_ptr->ethaddr), p);
         break;
      default:
         pbuf_free(p);
         break;
      }
   }

   return ERR_OK;
}

/*---------------------------------------------------------------------------*/
/* xemacif_setmac():                                                         */
/*                                                                           */
/* Sets the MAC address of the system.                                       */
/* Note:  Should only be called before xemacif_init is called.               */
/*          - the stack calls xemacif_init after the user calls netif_add    */
/*---------------------------------------------------------------------------*/
void xemacif_setmac(u32_t index, u8_t *addr)
{
   XEmacIf_ConfigTable[index].ethaddr.addr[0] = addr[0];
   XEmacIf_ConfigTable[index].ethaddr.addr[1] = addr[1];
   XEmacIf_ConfigTable[index].ethaddr.addr[2] = addr[2];
   XEmacIf_ConfigTable[index].ethaddr.addr[3] = addr[3];
   XEmacIf_ConfigTable[index].ethaddr.addr[4] = addr[4];
   XEmacIf_ConfigTable[index].ethaddr.addr[5] = addr[5];
}

/*---------------------------------------------------------------------------*/
/* xemacif_getmac():                                                         */
/*                                                                           */
/* Returns a pointer to the ethaddr variable in  the ConfigTable             */
/* (6 bytes in length)                                                       */
/*---------------------------------------------------------------------------*/
u8_t * xemacif_getmac(u32_t index) {
   return &(XEmacIf_ConfigTable[index].ethaddr.addr[0]);
}

/*---------------------------------------------------------------------------*/
/* xemacif_init():                                                           */
/*                                                                           */
/* Should be called at the beginning of the program to set up the            */
/* network interface. It calls the function low_level_init() to do the       */
/* actual setup of the hardware.                                             */
/*---------------------------------------------------------------------------*/
err_t xemacif_init(struct netif *netif_ptr)
{
   XEmacIf_Config *xemacif_ptr;

   xemacif_ptr = (XEmacIf_Config *) netif_ptr->state;

   netif_ptr->mtu = 1500;
   netif_ptr->hwaddr_len = 6;
   netif_ptr->hwaddr[0] = xemacif_ptr->ethaddr.addr[0];
   netif_ptr->hwaddr[1] = xemacif_ptr->ethaddr.addr[1];
   netif_ptr->hwaddr[2] = xemacif_ptr->ethaddr.addr[2];
   netif_ptr->hwaddr[3] = xemacif_ptr->ethaddr.addr[3];
   netif_ptr->hwaddr[4] = xemacif_ptr->ethaddr.addr[4];
   netif_ptr->hwaddr[5] = xemacif_ptr->ethaddr.addr[5];
   netif_ptr->name[0] = IFNAME0;
   netif_ptr->name[1] = IFNAME1;
   netif_ptr->output = etharp_output;
   netif_ptr->linkoutput = low_level_output;

   /* removed this statement because the ethaddr in the XEmacIf_Config
    * structure is now a struct not a pointer to a struct
    */
   //xemacif_ptr->ethaddr = (struct eth_addr *)&(netif_ptr->hwaddr[0]);

   low_level_init(netif_ptr);
   etharp_init();

   return ERR_OK;
}
