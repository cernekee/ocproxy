/*
 * Copyright (c) 2001-2003, Swedish Institute of Computer Science.
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
 * Author: David Haas dhaas@alum.rpi.edu
 *
 */

/* This is an ethernet driver for the internal fec in the Coldfire MCF5272.
   The driver has been written to use ISRs for Receive Frame and Transmit Frame
   Complete.
*/

#include "lwip/debug.h"

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/sys.h"

#include "netif/etharp.h"

#include "arch/mcf5272.h"

/* Sizing the descriptor rings will depend upon how many pbufs you have available
 * and how big they are. Also on how many frames you might want to input before dropping
 * frames. Generally it is a good idea to buffer one tcp window. This means that
 * you won't get a tcp retransmit and your tcp transmissions will be reasonably fast.
 */
#define NUM_RXBDS 64            // Number of receive descriptor rings
#define NUM_TXBDS 32            // Number of transmit descriptor rings

/* Define those to better describe your network interface. */
#define IFNAME0 'e'
#define IFNAME1 't'

/* Define interface MTU size. We set this to 1518, since this is the max
   Size of an ethernet frame without VLAN support. */
#define MTU_FEC 1518

PACK_STRUCT_BEGIN
struct rxbd 
{
    u16_t flags;
    u16_t data_len;
    u8_t *p_buf;
};
PACK_STRUCT_END
typedef struct rxbd rxbd_t;

PACK_STRUCT_BEGIN
struct txbd 
{
    u16_t flags;
    u16_t data_len;
    u8_t *p_buf;
};
PACK_STRUCT_END
typedef struct txbd txbd_t;

ALIGN_STRUCT_8_BEGIN
struct mcf5272if 
{
    rxbd_t rxbd_a[NUM_RXBDS];           // Rx descriptor ring. Must be aligned to double-word
    txbd_t txbd_a[NUM_TXBDS];           // Tx descriptor ring. Must be aligned to double-word
    struct pbuf *rx_pbuf_a[NUM_RXBDS];  // Array of pbufs corresponding to payloads in rx desc ring.
    struct pbuf *tx_pbuf_a[NUM_TXBDS];  // Array of pbufs corresponding to payloads in tx desc ring.
    unsigned int rx_remove;             // Index that driver will remove next rx frame from.
    unsigned int rx_insert;             // Index that driver will insert next empty rx buffer.
    unsigned int tx_insert;             // Index that driver will insert next tx frame to.
    unsigned int tx_remove;             // Index that driver will clean up next tx buffer.
    unsigned int tx_free;               // Number of free transmit descriptors.
    unsigned int rx_buf_len;            // number of bytes in a rx buffer (that we can use).
    MCF5272_IMM *imm;                   // imm address. All register accesses use this as base.
    struct eth_addr *ethaddr;
    struct netif *netif;
};
ALIGN_STRUCT_END

typedef struct mcf5272if mcf5272if_t;

#define INC_RX_BD_INDEX(idx) do { if (++idx >= NUM_RXBDS) idx = 0; } while (0)
#define INC_TX_BD_INDEX(idx) do { if (++idx >= NUM_TXBDS) idx = 0; } while (0)
#define DEC_TX_BD_INDEX(idx) do { if (idx-- == 0) idx = NUM_TXBDS-1; } while (0)

static mcf5272if_t *mcf5272if;
static sys_sem_t tx_sem;

u32_t phy;

typedef struct mcf5272if mcf5272if_t;

/*-----------------------------------------------------------------------------------*/
static void
fill_rx_ring(mcf5272if_t *mcf5272)
{
    struct pbuf *p;
    struct rxbd *p_rxbd;
    int i = mcf5272->rx_insert;
    void *new_payload;
    u32_t u_p_pay;
    
    /* Try and fill as many receive buffers as we can */
    while (mcf5272->rx_pbuf_a[i] == 0)
    {
        p = pbuf_alloc(PBUF_RAW, (u16_t) mcf5272->rx_buf_len, PBUF_POOL);
        if (p == 0)
            /* No pbufs, so can't refill ring */
            return;
        /* Align payload start to be divisible by 16 as required by HW */
        u_p_pay = (u32_t) p->payload;
        new_payload = p->payload = (void *) (((u_p_pay + 15) / 16) * 16);
        
        mcf5272->rx_pbuf_a[i] = p;
        p_rxbd = &mcf5272->rxbd_a[i];
        p_rxbd->p_buf = (u8_t *) new_payload;
        p_rxbd->flags = (p_rxbd->flags & MCF5272_FEC_RX_BD_W) | MCF5272_FEC_RX_BD_E;
        INC_RX_BD_INDEX(mcf5272->rx_insert);
        i = mcf5272->rx_insert;
    }
}

/*-----------------------------------------------------------------------------------*/
static void
enable_fec(mcf5272if_t *mcf5272)
{
    MCF5272_IMM *imm = mcf5272->imm;
    int i;

    /* Initialize empty tx descriptor ring */
    for(i = 0; i < NUM_TXBDS-1; i++)
        mcf5272->txbd_a[i].flags = 0;
    /* Set wrap bit for last descriptor */
    mcf5272->txbd_a[i].flags = MCF5272_FEC_TX_BD_W;
    /* initialize tx indexes */
    mcf5272->tx_remove = mcf5272->tx_insert = 0;
    mcf5272->tx_free = NUM_TXBDS;

    /* Initialize empty rx descriptor ring */
    for (i = 0; i < NUM_RXBDS-1; i++)
        mcf5272->rxbd_a[i].flags = 0;
    /* Set wrap bit for last descriptor */
    mcf5272->rxbd_a[i].flags = MCF5272_FEC_RX_BD_W;
    /* Initialize rx indexes */
    mcf5272->rx_remove = mcf5272->rx_insert = 0;

    /* Fill receive descriptor ring */
    fill_rx_ring(mcf5272);
    
    /* Enable FEC */
    MCF5272_WR_FEC_ECR(imm, (MCF5272_FEC_ECR_ETHER_EN));// | 0x2000000));

    /* Indicate that there have been empty receive buffers produced */
    MCF5272_WR_FEC_RDAR(imm,1);
}


/*-----------------------------------------------------------------------------------*/
static void
disable_fec(mcf5272if_t *mcf5272)
{
    MCF5272_IMM *imm = mcf5272->imm;
    int i;
    u32_t value;
    u32_t old_level;

    /* We need to disable interrupts here, It is important when dealing with shared
       registers. */
    old_level = sys_arch_protect();
    
    /* First disable the FEC interrupts. Do it in the appropriate ICR register. */
    value = MCF5272_RD_SIM_ICR3(imm);
    MCF5272_WR_SIM_ICR3(imm, (value & ~(MCF5272_SIM_ICR_ERX_IL(7) |
                                       MCF5272_SIM_ICR_ETX_IL(7) |
                                       MCF5272_SIM_ICR_ENTC_IL(7))));

    /* Now we can restore interrupts. This is because we can assume that
     * we are single threaded here (only 1 thread will be calling disable_fec
     * for THIS interface). */
    sys_arch_unprotect(old_level);
    
    /* Release all buffers attached to the descriptors.  Since the driver
     * ALWAYS zeros the pbuf array locations and descriptors when buffers are
     * removed, we know we just have to free any non-zero descriptors */
    for (i = 0; i < NUM_RXBDS; i++)
        if (mcf5272->rx_pbuf_a[i])
        {
            pbuf_free(mcf5272->rx_pbuf_a[i]);
            mcf5272->rx_pbuf_a[i] = 0;
            mcf5272->rxbd_a->p_buf = 0;
        }
    for (i = 0; i < NUM_TXBDS; i++)
        if (mcf5272->tx_pbuf_a[i])
        {
            pbuf_free(mcf5272->tx_pbuf_a[i]);
            mcf5272->tx_pbuf_a[i] = 0;
            mcf5272->txbd_a->p_buf = 0;
        }
    
    /* Reset the FEC - equivalent to a hard reset */
    MCF5272_WR_FEC_ECR(imm,MCF5272_FEC_ECR_RESET);
    /* Wait for the reset sequence to complete, it should take about 16 clock cycles */
    i = 0;
    while (MCF5272_RD_FEC_ECR(imm) & MCF5272_FEC_ECR_RESET) 
    {
        if (++i > 100)
            abort();
    }
    
    /* Disable all FEC interrupts by clearing the IMR register */
    MCF5272_WR_FEC_IMR(imm,0);
    /* Clear any interrupts by setting all bits in the EIR register */
    MCF5272_WR_FEC_EIR(imm,0xFFFFFFFF);
}

/*-----------------------------------------------------------------------------------*
 * Function called by receive LISR to disable fec tx interrupt
 *-----------------------------------------------------------------------------------*/
static void
mcf5272_dis_tx_int(void)
{
    mcf5272if_t *mcf5272 = mcf5272if;
    MCF5272_IMM *imm = mcf5272->imm;
    u32_t value;

    value = MCF5272_RD_FEC_IMR(imm);
    /* Clear rx interrupt bit */
    MCF5272_WR_FEC_IMR(imm, (value & ~MCF5272_FEC_IMR_TXFEN));
    return;
}

/*-----------------------------------------------------------------------------------*
 *-----------------------------------------------------------------------------------*/
static void mcf5272fec_tx_hisr(void)
{
    /* Just signal task that it can run and cleanup */
    sys_sem_signal(tx_sem);
}

/*-----------------------------------------------------------------------------------*
  This function must be run as a task, since it ends up calling free() through pbuf_free()
 *-----------------------------------------------------------------------------------*/
static void
mcf5272fec_tx_cleanup(void)
{
    struct pbuf *p;
    mcf5272if_t *mcf5272 = mcf5272if;
    MCF5272_IMM *imm = mcf5272->imm;
    u32_t value;
    u32_t old_level;
    unsigned int tx_remove_sof;
    unsigned int tx_remove_eof;
    unsigned int i;
    u16_t flags;
    

    tx_remove_sof = tx_remove_eof = mcf5272->tx_remove;
    /* We must protect reading the flags and then reading the buffer pointer. They must
       both be read together. */
    old_level = sys_arch_protect();
    /* Loop, looking for completed buffers at eof */
    while ((((flags = mcf5272->txbd_a[tx_remove_eof].flags) & MCF5272_FEC_TX_BD_R) == 0) &&
           (mcf5272->tx_pbuf_a[tx_remove_eof] != 0))
    {
        /* See if this is last buffer in frame */
        if ((flags & MCF5272_FEC_TX_BD_L) != 0)
        {
            i = tx_remove_eof;
            /* This frame is complete. Take the frame off backwards */
            do
            {
                p = mcf5272->tx_pbuf_a[i];
                mcf5272->tx_pbuf_a[i] = 0;
                mcf5272->txbd_a[i].p_buf = 0;
                mcf5272->tx_free++;
                if (i != tx_remove_sof)
                    DEC_TX_BD_INDEX(i);
                else
                    break;
            } while (1);

            sys_arch_unprotect(old_level);
            pbuf_free(p);       // Will be head of chain
            old_level = sys_arch_protect();
            /* Look at next descriptor */
            INC_TX_BD_INDEX(tx_remove_eof);
            tx_remove_sof = tx_remove_eof;
        }
        else
            INC_TX_BD_INDEX(tx_remove_eof);
    }
    mcf5272->tx_remove = tx_remove_sof;

    /* clear interrupt status for tx interrupt */
    MCF5272_WR_FEC_EIR(imm, MCF5272_FEC_EIR_TXF);
    value = MCF5272_RD_FEC_IMR(imm);
    /* Set tx interrupt bit again */
    MCF5272_WR_FEC_IMR(imm, (value | MCF5272_FEC_IMR_TXFEN));
    /* Now we can re-enable higher priority interrupts again */
    sys_arch_unprotect(old_level);
}


/*-----------------------------------------------------------------------------------*
  void low_level_output(mcf5272if_t *mcf5272, struct pbuf *p)

  Output pbuf chain to hardware. It is assumed that there is a complete and correct
  ethernet frame in p. The only buffering we have in this system is in the
  hardware descriptor ring. If there is no room on the ring, then drop the frame.
 *-----------------------------------------------------------------------------------*/
static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
    struct pbuf *q;
    mcf5272if_t *mcf5272 = netif->state;
    MCF5272_IMM *imm = mcf5272->imm;
    int num_desc;
    int num_free;
    unsigned int tx_insert_sof, tx_insert_eof;
    unsigned int i;
    u32_t old_level;

    /* Make sure that there are no PBUF_REF buffers in the chain. These buffers
       have to be freed immediately and this ethernet driver puts the buffers on
       the dma chain, so they get freed later */
    p = pbuf_take(p);
    /* Interrupts are disabled through this whole thing to support multi-threading
     * transmit calls. Also this function might be called from an ISR. */
    old_level = sys_arch_protect();
    
    /* Determine number of descriptors needed */
    num_desc = pbuf_clen(p);
    if (num_desc > mcf5272->tx_free)
    {
        /* Drop the frame, we have no place to put it */
#ifdef LINK_STATS
        lwip_stats.link.memerr++;
#endif
        sys_arch_unprotect(old_level);
        return ERR_MEM;
        
    } else {
        /* Increment use count on pbuf */
        pbuf_ref(p);
        
        /* Put buffers on descriptor ring, but don't mark them as ready yet */
        tx_insert_eof = tx_insert_sof = mcf5272->tx_insert;
        q = p;
        do
        {
            mcf5272->tx_free--;
            mcf5272->tx_pbuf_a[tx_insert_eof] = q;
            mcf5272->txbd_a[tx_insert_eof].p_buf = q->payload;
            mcf5272->txbd_a[tx_insert_eof].data_len = q->len;
            q = q->next;
            if (q)
                INC_TX_BD_INDEX(tx_insert_eof);
        } while (q);
        
        /* Go backwards through descriptor ring setting flags */
        i = tx_insert_eof;
        do
        {
            mcf5272->txbd_a[i].flags = (u16_t) (MCF5272_FEC_TX_BD_R |
                                                (mcf5272->txbd_a[i].flags & MCF5272_FEC_TX_BD_W) |
                               ((i == tx_insert_eof) ? (MCF5272_FEC_TX_BD_L | MCF5272_FEC_TX_BD_TC) : 0));
            if (i != tx_insert_sof)
                DEC_TX_BD_INDEX(i);
            else
                break;
        } while (1);
        INC_TX_BD_INDEX(tx_insert_eof);
        mcf5272->tx_insert = tx_insert_eof;
#ifdef LINK_STATS
        lwip_stats.link.xmit++;
#endif        
	/* Indicate that there has been a transmit buffer produced */
	MCF5272_WR_FEC_TDAR(imm,1);
        sys_arch_unprotect(old_level);
    }
    return ERR_OK;
}

/*-----------------------------------------------------------------------------------*/
static void
eth_input(struct pbuf *p, struct netif *netif)
{
    /* Ethernet protocol layer */
    struct eth_hdr *ethhdr;
    mcf5272if_t *mcf5272 = netif->state;

    ethhdr = p->payload;
    
    switch (htons(ethhdr->type)) {
      case ETHTYPE_IP:
        etharp_ip_input(netif, p);
        pbuf_header(p, -14);
        netif->input(p, netif);
        break;
      case ETHTYPE_ARP:
        etharp_arp_input(netif, mcf5272->ethaddr, p);
        break;
      default:
        pbuf_free(p);
        break;
    }
}

/*-----------------------------------------------------------------------------------*/
static void
arp_timer(void *arg)
{
  etharp_tmr();
  sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler)arp_timer, NULL);
}

/*-----------------------------------------------------------------------------------*
 * Function called by receive LISR to disable fec rx interrupt
 *-----------------------------------------------------------------------------------*/
static void
mcf5272_dis_rx_int(void)
{
    mcf5272if_t *mcf5272 = mcf5272if;
    MCF5272_IMM *imm = mcf5272->imm;
    u32_t value;

    value = MCF5272_RD_FEC_IMR(imm);
    /* Clear rx interrupt bit */
    MCF5272_WR_FEC_IMR(imm, (value & ~MCF5272_FEC_IMR_RXFEN));
    return;
}

/*-----------------------------------------------------------------------------------*/
static void
mcf5272fec_rx(void)
{
    /* This is the receive ISR. It is written to be a high-level ISR. */
    u32_t old_level;
    mcf5272if_t *mcf5272 = mcf5272if;
    MCF5272_IMM *imm = mcf5272->imm;
    u32_t value;
    u16_t flags;
    unsigned int rx_remove_sof;
    unsigned int rx_remove_eof;
    struct pbuf *p;
    

    rx_remove_sof = rx_remove_eof = mcf5272->rx_remove;

    /* Loop, looking for filled buffers at eof */
    while ((((flags = mcf5272->rxbd_a[rx_remove_eof].flags) & MCF5272_FEC_RX_BD_E) == 0) &&
           (mcf5272->rx_pbuf_a[rx_remove_eof] != 0))
    {
        /* See if this is last buffer in frame */
        if ((flags & MCF5272_FEC_RX_BD_L) != 0)
        {
            /* This frame is ready to go. Start at first descriptor in frame. */
            p = 0;
            do
            {
                /* Adjust pbuf length if this is last buffer in frame */
                if (rx_remove_sof == rx_remove_eof)
                {
                    mcf5272->rx_pbuf_a[rx_remove_sof]->tot_len =
                        mcf5272->rx_pbuf_a[rx_remove_sof]->len = (u16_t)
                        (mcf5272->rxbd_a[rx_remove_sof].data_len - (p ? p->tot_len : 0));
                }
                else
                    mcf5272->rx_pbuf_a[rx_remove_sof]->len =
                        mcf5272->rx_pbuf_a[rx_remove_sof]->tot_len = mcf5272->rxbd_a[rx_remove_sof].data_len;
                
                /* Chain pbuf */
                if (p == 0)
                {
                    p = mcf5272->rx_pbuf_a[rx_remove_sof];       // First in chain
                    p->tot_len = p->len;                        // Important since len might have changed
                } else {
                    pbuf_chain(p, mcf5272->rx_pbuf_a[rx_remove_sof]);
                    pbuf_free(mcf5272->rx_pbuf_a[rx_remove_sof]);
                }
                
                /* Clear pointer to mark descriptor as free */
                mcf5272->rx_pbuf_a[rx_remove_sof] = 0;
                mcf5272->rxbd_a[rx_remove_sof].p_buf = 0;
                
                if (rx_remove_sof != rx_remove_eof)
                    INC_RX_BD_INDEX(rx_remove_sof);
                else
                    break;
               
            } while (1);
            INC_RX_BD_INDEX(rx_remove_sof);

            /* Check error status of frame */
            if (flags & (MCF5272_FEC_RX_BD_LG |
                         MCF5272_FEC_RX_BD_NO |
                         MCF5272_FEC_RX_BD_CR |
                         MCF5272_FEC_RX_BD_OV))
            {
#ifdef LINK_STATS
                lwip_stats.link.drop++;
                if (flags & MCF5272_FEC_RX_BD_LG)
                    lwip_stats.link.lenerr++;                //Jumbo gram
                else
                    if (flags & (MCF5272_FEC_RX_BD_NO | MCF5272_FEC_RX_BD_OV))
                        lwip_stats.link.err++;
                    else
                        if (flags & MCF5272_FEC_RX_BD_CR)
                            lwip_stats.link.chkerr++;        // CRC errors
#endif
                /* Drop errored frame */
                pbuf_free(p);
            } else {
                /* Good frame. increment stat */
#ifdef LINK_STATS
                lwip_stats.link.recv++;
#endif                
                eth_input(p, mcf5272->netif);
            }
        }
        INC_RX_BD_INDEX(rx_remove_eof);
    }
    mcf5272->rx_remove = rx_remove_sof;
    
    /* clear interrupt status for rx interrupt */
    old_level = sys_arch_protect();
    MCF5272_WR_FEC_EIR(imm, MCF5272_FEC_EIR_RXF);
    value = MCF5272_RD_FEC_IMR(imm);
    /* Set rx interrupt bit again */
    MCF5272_WR_FEC_IMR(imm, (value | MCF5272_FEC_IMR_RXFEN));
    /* Now we can re-enable higher priority interrupts again */
    sys_arch_unprotect(old_level);

    /* Fill up empty descriptor rings */
    fill_rx_ring(mcf5272);
    /* Tell fec that we have filled up her ring */
    MCF5272_WR_FEC_RDAR(imm, 1);

    return;
}

            

/*-----------------------------------------------------------------------------------*/
static void
low_level_init(struct netif *netif)
{
    mcf5272if_t *mcf5272;
    MCF5272_IMM *imm;
    VOID        (*old_lisr)(INT);   /* old LISR */
    u32_t value;
    u32_t old_level;
    struct pbuf *p;
    int i;

    mcf5272 = netif->state;
    imm = mcf5272->imm;

    /* Initialize our ethernet address */
    sys_get_eth_addr(mcf5272->ethaddr);
    
    /* First disable fec */
    disable_fec(mcf5272);

    /* Plug appropriate low level interrupt vectors */
    sys_setvect(MCF5272_VECTOR_ERx, mcf5272fec_rx, mcf5272_dis_rx_int);
    sys_setvect(MCF5272_VECTOR_ETx, mcf5272fec_tx_hisr, mcf5272_dis_tx_int);
    //sys_setvect(MCF5272_VECTOR_ENTC, mcf5272fec_ntc);

    /* Set the I_MASK register to enable only rx & tx frame interrupts */
    MCF5272_WR_FEC_IMR(imm, MCF5272_FEC_IMR_TXFEN | MCF5272_FEC_IMR_RXFEN);

    /* Clear I_EVENT register */
    MCF5272_WR_FEC_EIR(imm,0xFFFFFFFF);

    /* Set up the appropriate interrupt levels */
    /* Disable interrupts, since this is a read/modify/write operation */
    old_level = sys_arch_protect();
    value = MCF5272_RD_SIM_ICR3(imm);
    MCF5272_WR_SIM_ICR3(imm, value | MCF5272_SIM_ICR_ERX_IL(FEC_LEVEL) |
                        MCF5272_SIM_ICR_ETX_IL(FEC_LEVEL));
    sys_arch_unprotect(old_level);
    
    /* Set the source address for the controller */
    MCF5272_WR_FEC_MALR(imm,0 
                        | (mcf5272->ethaddr->addr[0] <<24) 
                        | (mcf5272->ethaddr->addr[1] <<16)	
                        | (mcf5272->ethaddr->addr[2] <<8) 
                        | (mcf5272->ethaddr->addr[3] <<0)); 
    MCF5272_WR_FEC_MAUR(imm,0
                        | (mcf5272->ethaddr->addr[4] <<24)
                        | (mcf5272->ethaddr->addr[5] <<16));
    
    /* Initialize the hash table registers */
    /* We are not supporting multicast addresses */
    MCF5272_WR_FEC_HTUR(imm,0);
    MCF5272_WR_FEC_HTLR(imm,0);

    /* Set Receive Buffer Size. We subtract 16 because the start of the receive
    *  buffer MUST be divisible by 16, so depending on where the payload really
    *  starts in the pbuf, we might be increasing the start point by up to 15 bytes.
    *  See the alignment code in fill_rx_ring() */
    /* There might be an offset to the payload address and we should subtract
     * that offset */
    p = pbuf_alloc(PBUF_RAW, PBUF_POOL_BUFSIZE, PBUF_POOL);
    i = 0;
    if (p)
    {
        struct pbuf *q = p;
        
        while ((q = q->next) != 0)
            i += q->len;
        mcf5272->rx_buf_len = PBUF_POOL_BUFSIZE-16-i;
        pbuf_free(p);
    }
    
    
    MCF5272_WR_FEC_EMRBR(imm, (u16_t) mcf5272->rx_buf_len);

    /* Point to the start of the circular Rx buffer descriptor queue */
    MCF5272_WR_FEC_ERDSR(imm, ((u32_t) &mcf5272->rxbd_a[0]));

    /* Point to the start of the circular Tx buffer descriptor queue */
    MCF5272_WR_FEC_ETDSR(imm, ((u32_t) &mcf5272->txbd_a[0]));
    
    /* Set the tranceiver interface to MII mode */
    MCF5272_WR_FEC_RCR(imm, 0
                       | MCF5272_FEC_RCR_MII_MODE
                       | MCF5272_FEC_RCR_DRT); 	/* half duplex */

    /* Only operate in half-duplex, no heart beat control */
    MCF5272_WR_FEC_TCR(imm, 0);

    /* Set the maximum frame length (MTU) */
    MCF5272_WR_FEC_MFLR(imm, MTU_FEC);

    /* Set MII bus speed */
    MCF5272_WR_FEC_MSCR(imm, 0x0a);

    /* Enable fec i/o pins */
    value = MCF5272_RD_GPIO_PBCNT(imm);
    MCF5272_WR_GPIO_PBCNT(imm, ((value & 0x0000ffff) | 0x55550000));

    /* Clear MII interrupt status */
    MCF5272_WR_FEC_EIR(imm, MCF5272_FEC_IMR_MIIEN);
        
/*     /\* Read phy ID *\/ */
/*     MCF5272_WR_FEC_MMFR(imm, 0x600a0000); */
/*     while (1) */
/*     { */
/*         value = MCF5272_RD_FEC_EIR(imm); */
/*         if ((value & MCF5272_FEC_IMR_MIIEN) != 0) */
/*         { */
/*             MCF5272_WR_FEC_EIR(imm, MCF5272_FEC_IMR_MIIEN); */
/*             break; */
/*         } */
/*     } */
/*     phy = MCF5272_RD_FEC_MMFR(imm); */
    
    /* Enable FEC */
    enable_fec(mcf5272);

    /* THIS IS FOR LEVEL ONE/INTEL PHY ONLY!!! */
    /* Program Phy LED 3 to tell us transmit status */
    MCF5272_WR_FEC_MMFR(imm, 0x50520412);

}

/*-----------------------------------------------------------------------------------*
 * etharp timer thread
 * It's only job is to initialize the timer, create a semaphore and wait on it
 * forever. We need a special task to handle the arp timer.
 *-----------------------------------------------------------------------------------*/
static void
etharp_timer_thread(void *arg)
{
    sys_sem_t *psem = (sys_sem_t *) arg;
    
    /* Create timeout timer */
    sys_timeout(ARP_TMR_INTERVAL, (sys_timeout_handler)arp_timer, NULL);
    /* Signal previous task that it can go */
    sys_sem_signal(*psem);

    tx_sem = sys_sem_new(0);

    while (1)
    {
        sys_sem_wait(tx_sem);
        mcf5272fec_tx_cleanup();
    }
}


    
/*-----------------------------------------------------------------------------------*/
static void
etharp_timer_init(void *arg)
{
    sys_thread_new(DEFAULT_THREAD_NAME, (void *)etharp_timer_thread, arg, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
}


/*-----------------------------------------------------------------------------------*/
/*
 * mcf5272fecif_init(struct netif *netif):
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * Note that there is only one fec in a 5272!
 *
 */
err_t
mcf5272fecif_init(struct netif *netif)
{
    sys_sem_t sem;
    
    /* Allocate our interface control block */
    /* IMPORTANT NOTE: This works for 5272, but if you are using a cpu with data cache
     * then you need to make sure you get this memory from non-cachable memory. */
    mcf5272if = (mcf5272if_t *) calloc(1, sizeof(mcf5272if_t));
    if (mcf5272if)
    {
        netif->state = mcf5272if;
        mcf5272if->netif = netif;
        netif->name[0] = IFNAME0;
        netif->name[1] = IFNAME1;
        netif->output = etharp_output;
        netif->linkoutput = low_level_output;
        netif->mtu = MTU_FEC - 18;      // mtu without ethernet header and crc
        mcf5272if->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);
        netif->hwaddr_len = 6; /* Ethernet interface */
        mcf5272if->imm = mcf5272_get_immp();
        
        low_level_init(netif);

        etharp_init();
        sem = sys_sem_new(0);
        etharp_timer_init(&sem);
        sys_sem_wait(sem);
        sys_sem_free(sem);
        
        return ERR_OK;
    }
    else
        return ERR_MEM;
}

/*-----------------------------------------------------------------------------------*/
