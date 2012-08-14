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

	netif = (struct netif *)arg;
	ocif = (struct ocif *)netif->state;

	while (1) {
		char buf[2048];
		int len;
		struct pbuf *p;

		if ((len = read(ocif->fd, buf, 2048)) < 0) {
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_input: failed to read data (%d).\n", len));
			continue;
		}
		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_input: read %d bytes.\n", len));

		if ((p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL)) != NULL) {
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
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_input: could not allocate pbuf.\n"));
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
	ssize_t r;
	LWIP_UNUSED_ARG(ipaddr);

	ocif = (struct ocif *)netif->state;

	pbuf_ref(p);

	plen = p->tot_len;

	if (plen == 0) {
		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: packet length == 0!\n"));
		goto done;
	}

	for (q = p, nchunks = 0; q != NULL; q = q->next, nchunks++) {
		if (nchunks > MAX_IOVEC)
			break;

		ocif->iov[nchunks].iov_base = q->payload;
		ocif->iov[nchunks].iov_len = q->len;
	}

	if (nchunks <= MAX_IOVEC) {
		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: sending %d (%d) bytes (writev).\n", p->len, plen));

		if ((r = writev(ocif->fd, (const struct iovec *)&ocif->iov, nchunks)) != plen) {
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: failed to write %d bytes (%ld) (writev).\n", plen, r));
			goto done;
		}
	} else {
		/* Have to copy. */
		data = (char *)malloc(plen);
		if (data == NULL) {
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: cannot allocated %d bytes for packet copy.\n", plen));
			goto done;
		}

		if (pbuf_copy_partial(p, data, plen, 0) != plen) {
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: failed to copy %d bytes of data.\n", plen));
			goto free;
		}

		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: sending %d (%d) bytes.\n", p->len, plen));

		if ((r = write(ocif->fd, data, plen)) != plen) {
			LWIP_DEBUGF(OCIF_DEBUG, ("ocif_output: failed to write %d bytes (%ld) (write).\n", plen, r));
			goto free;
		}

	free:
		free(data);
	}

 done:
	tcpdump(p);
	LINK_STATS_INC(link.xmit);

	pbuf_free(p);
}

err_t
ocif_init_client(struct netif *netif)
{
	char *vpnfd;
	struct ocif *ocif;

	ocif = (struct ocif *)malloc(sizeof (*ocif));
	memset(ocif, 0, sizeof (*ocif));

	netif->state = ocif;
	netif->name[0] = 'u';
	netif->name[1] = 'n';
	netif->output = ocif_output;

	if ((vpnfd = getenv("VPNFD")) == NULL) {
		LWIP_DEBUGF(OCIF_DEBUG, ("ocif_init_client: no VPNFD?\n"));
		abort();
	}

	ocif->fd = (int)strtoul(vpnfd, (char **)NULL, 10);
	if (errno != 0) {
		perror("ocif_init_client: cannot parse VPNFD");
		abort();
	}

	sys_thread_new("ocif_input", ocif_input, netif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);

	return ERR_OK;
}
