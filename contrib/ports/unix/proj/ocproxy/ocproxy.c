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
 * Author: David Edmondson <dme@dme.org>
 */

/*
 * Derived from simhost.c.
 */

#include "lwip/debug.h"
#include "lwip/opt.h"

#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/tcp_impl.h"

#include "lwip/stats.h"

#include "lwip/tcpip.h"
#include "lwip/dns.h"

#include "netif/dropif.h"

#include "netif/tcpdump.h"

#include "lwip/ip_addr.h"

#include "arch/perf.h"

#include "ocif.h"
#include "tcpfw.h"

/* nonstatic debug cmd option, exported in lwipopts.h */
unsigned char debug_flags = 0;

typedef struct fwd {
	in_port_t lport;
	char *rhost;
	in_port_t rport;
	struct fwd *next;
} fwd_t;

ip_addr_t ipaddr, netmask, gw, dns;
in_port_t socks_port;
fwd_t *forwards;

struct netif netif_oc;

static void
tcpip_init_done(void *arg)
{
	sys_sem_t *sem = (sys_sem_t *)arg;
	fwd_t *fwd;

	netif_init();

	netif_oc.name[0] = 'u';
	netif_oc.name[1] = '0';

	netif_add(&netif_oc, &ipaddr, &netmask, &gw, NULL, ocif_init_client,
		  tcpip_input);

	netif_set_default(&netif_oc);
	netif_set_up(&netif_oc);

	dns_init();
	dns_setserver(0, &dns);

	if (socks_port != 0)
		tcpsocks_init(socks_port);

	for (fwd = forwards; fwd != NULL; fwd = fwd->next)
		tcpfw_init(fwd->lport, fwd->rhost, fwd->rport);

	sys_sem_signal(sem);
}

static void
main_thread(void *arg)
{
	sys_sem_t sem;
	LWIP_UNUSED_ARG(arg);

	if(sys_sem_new(&sem, 0) != ERR_OK) {
		LWIP_ASSERT("Failed to create semaphore", 0);
	}
	tcpip_init(tcpip_init_done, &sem);
	sys_sem_wait(&sem);

	/* Block forever. */
	sys_sem_wait(&sem);
}

static void
fwd_add(char *s)
{
	char *lport_s, *rhost_s, *rport_s;
	fwd_t *newp;

	lport_s = rhost_s = rport_s = NULL;

	lport_s = s;
	while (*s != '\0') {
		if (*s == ':') {
			*s = '\0';
			s++;
			break;
		}
		s++;
	}

	rhost_s = s;
	while (*s != '\0') {
		if (*s == ':') {
			*s = '\0';
			s++;
			break;
		}
		s++;
	}

	rport_s = s;

	newp = (fwd_t *)malloc(sizeof (*newp));
	newp->lport = atoi(lport_s);
	newp->rhost = rhost_s;
	newp->rport = atoi(rport_s);
	newp->next = NULL;

	if (forwards)
		newp->next = forwards;
	forwards = newp;
}

int
main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "i:n:g:d:D:L:v")) != -1) {
		switch (opt) {
		case 'i':
			ipaddr.addr = inet_addr(optarg);
			break;
		case 'n':
			netmask.addr = inet_addr(optarg);
			break;
		case 'g':
			gw.addr = inet_addr(optarg);
			break;
		case 'd':
			dns.addr = inet_addr(optarg);
			break;
		case 'D':
			socks_port = atoi(optarg);
			break;
		case 'L':
			fwd_add(optarg);
			break;
		case 'v':
			debug_flags = LWIP_DBG_ON | LWIP_DBG_TRACE | LWIP_DBG_STATE | LWIP_DBG_FRESH | LWIP_DBG_HALT;
			break;
		default:
			fprintf(stderr, "unknown option: %c\n", opt);
			break;
		}
	}

	if ((ipaddr.addr == 0) ||
	    (netmask.addr == 0) ||
	    (gw.addr == 0) ||
	    (dns.addr == 0)) {
		fprintf(stderr, "missing -i, -n, -g or -d\n");
		return (1);
	}

	if (debug_flags & LWIP_DBG_ON)
		tcpdump_init();

	/* Debugging help. */
	(void) signal(SIGPIPE, SIG_IGN);
	setlinebuf(stdout);
	setlinebuf(stderr);

	sys_thread_new("main_thread", main_thread, NULL, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
	pause();

	return (0);
}

