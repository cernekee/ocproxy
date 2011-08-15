/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
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
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

/*
 * Derived from unixif.c.
 */

#include "lwip/debug.h"

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "lwip/stats.h"

#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "netif/list.h"
#include "lwip/sys.h"

#include "netif/tcpdump.h"

#include "ocif.h"

#ifndef OCIF_DEBUG
#define OCIF_DEBUG LWIP_DBG_OFF
#endif

#define MAX_IOVEC 16

struct ocif {
	int fd;
	struct iovec iov[MAX_IOVEC];
};

static void
ocif_input(void *arg)
{
	struct netif *netif;
	struct ocif *ocif;
	char buf[2048];
	int len;
	struct pbuf *p;

	netif = (struct netif *)arg;
	ocif = (struct ocif *)netif->state;

	while (1) {
		len = read(ocif->fd, buf, 2048);
		if (len == -1) {
			perror("ocif_irq_handler: read");
			abort();
		}
		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_irq_handler: read %d bytes\n", len));
		p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

		if (p != NULL) {
			char *bufptr;
			struct pbuf *q;

			bufptr = buf;
			q = p;
			while (len > 0) {
				int copy = (len > q->len) ? q->len : len;

				memcpy(q->payload, bufptr, copy);
				len -= copy;
				bufptr += copy;
				q = q->next;
			}
			LINK_STATS_INC(link.recv);
			tcpdump(p);
			netif->input(p, netif);
		} else {
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_irq_handler: could not allocate pbuf\n"));
		}
	}
}

static err_t
ocif_output(struct netif *netif, struct pbuf *p, ip_addr_t *ipaddr)
{
	struct ocif *ocif;
	struct pbuf *q;
	int nchunks;
	unsigned short plen;
	char *data, *bp;
	LWIP_UNUSED_ARG(ipaddr);

	LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output\n"));

	ocif = (struct ocif *)netif->state;

	pbuf_ref(p);

	plen = p->tot_len;

	if (plen == 0) {
		LWIP_DEBUGF(OCIF_DEBUG, ("packet length == 0!\n"));
		abort();
	}

	for (q = p, nchunks = 0; q != NULL; q = q->next, nchunks++) {
		if (nchunks > MAX_IOVEC)
			break;

		ocif->iov[nchunks].iov_base = q->payload;
		ocif->iov[nchunks].iov_len = q->len;
	}

	if (nchunks <= MAX_IOVEC) {
		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: sending %d (%d) bytes (writev)\n",
					 p->len, plen));

		if (writev(ocif->fd, (const struct iovec *)&ocif->iov, nchunks) != plen) {
			LWIP_DEBUGF(OCIF_DEBUG, ("failed to write %d bytes (writev)\n",
						 plen));
			abort();
		}
	} else {
		/* Have to copy. */
		data = (char *)malloc(plen);

		if (pbuf_copy_partial(p, data, plen, 0) != plen) {
			LWIP_DEBUGF(OCIF_DEBUG, ("failed to copy data\n"));
			abort();
		}

		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: sending %d (%d) bytes\n",
					 p->len, plen));

		if (write(ocif->fd, data, plen) == -1) {
			perror("ocif_output: write");
			abort();
		}

		free(data);
	}

	tcpdump(p);
	LINK_STATS_INC(link.xmit);

	pbuf_free(p);
}

err_t
ocif_init_client(struct netif *netif)
{
	struct ocif *ocif;
	int fd = atoi(getenv("VPNFD"));

	ocif = (struct ocif *)malloc(sizeof(struct ocif));
	if (!ocif) {
		return ERR_MEM;
	}

	netif->state = ocif;
	netif->name[0] = 'u';
	netif->name[1] = 'n';
	netif->output = ocif_output;

	ocif->fd = fd;
	if (ocif->fd == -1) {
		perror("ocif_init");
		abort();
	}

	sys_thread_new("ocif_input", ocif_input, netif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);

	return ERR_OK;
}
