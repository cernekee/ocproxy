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
 * Author: Adam Dunkels <adam@sics.se>
 * Author: David Edmondson <dme@dme.org>
 * Author: Kevin Cernekee <cernekee@gmail.com>
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <event2/event.h>
#include <event2/listener.h>

#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/err.h"
#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "lwip/sys.h"
#include "lwip/tcp_impl.h"
#include "netif/tcpdump.h"

#define STATE_NEW		0
#define STATE_SOCKS_AUTH	1
#define STATE_SOCKS_CMD		2
#define STATE_DNS		3
#define STATE_CONNECTING	4
#define STATE_DATA		5
#define STATE_DEAD		6

#define CONN_TYPE_REDIR		0
#define CONN_TYPE_SOCKS		1

#define SOCKBUF_LEN		2048

#define FL_ACTIVATE		1
#define FL_DIE_ON_ERROR		2

#define MAX_IOVEC		16
#define MAX_CONN		32

#define SOCKS_VER		0x05

#define SOCKS_CMD_CONNECT	0x01
#define SOCKS_CMD_BIND		0x02
#define SOCKS_CMD_UDP_ASSOCIATE	0x03

#define SOCKS_OK		0x00
#define SOCKS_GEN_FAILURE	0x01
#define SOCKS_NOT_ALLOWED	0x02
#define SOCKS_NET_UNREACHABLE	0x03
#define SOCKS_HOST_UNREACHABLE	0x04
#define SOCKS_CONNREFUSED	0x05
#define SOCKS_TTLEXPIRED	0x06
#define SOCKS_CMDNOTSUPP	0x07
#define SOCKS_ADDRNOTSUPP	0x08

#define SOCKS_ATYP_IPV4		0x01
#define SOCKS_ATYP_DOMAIN	0x03
#define SOCKS_ATYP_IPV6		0x04

struct ocp_sock {
	/* general */
	int fd;
	struct evconnlistener *listener;
	struct event *ev;
	struct tcp_pcb *tpcb;
	int state;
	int conn_type;
	struct ocp_sock *next;

	/* for TCP send/receive */
	int done_len;
	int lwip_blocked;
	int sock_pos;
	int sock_total;
	char sockbuf[SOCKBUF_LEN];

	/* for port forwarding */
	char *rhost_name;
	ip_addr_t rhost;
	int rport;

	/* for lwip_data_cb() */
	struct netif *netif;
};

struct socks_auth {
	u8_t ver;
	u8_t n_methods;
	u8_t methods;
} PACK_STRUCT_STRUCT;

struct socks_req {
	u8_t ver;
	u8_t cmd;
	u8_t rsv;
	u8_t atyp;
	union {
		struct {
			u32_t dst_addr;
			u16_t dst_port;
			u8_t end;
		} ipv4;
		struct {
			u8_t fqdn_len;
			u8_t fqdn_name[255]; /* variable length */
			u16_t port;
		} fqdn;
	} u;
} PACK_STRUCT_STRUCT;

struct socks_reply {
	u8_t ver;
	u8_t rep;
	u8_t rsv;
	u8_t atyp;
	u32_t bnd_addr;
	u16_t bnd_port;
} PACK_STRUCT_STRUCT;

static struct event_base *event_base;

static struct ocp_sock ocp_sock_pool[MAX_CONN];
static struct ocp_sock *ocp_sock_free_list;

/* nonstatic debug cmd option, exported in lwipopts.h */
unsigned char debug_flags = 0;

static int allow_remote;
static int tcpdump_enabled;
static int keep_intvl;
static int got_sighup;
static int got_sigusr1;

static void start_connection(const char *hostname, ip_addr_t *ipaddr, void *arg);
static void start_resolution(struct ocp_sock *s, const char *hostname);

/**********************************************************************
 * Utility functions / libevent wrappers
 **********************************************************************/

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fflush(stdout);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fflush(stdout);
	fprintf(stderr, "warning: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static char *xstrdup(const char *s)
{
	char *ret = strdup(s);
	if (!ret)
		die("out of memory\n");
	return ret;
}

static int ocp_atoi(const char *s)
{
	char *p;
	long val = strtol(s, &p, 0);
	if (!*s || *p)
		die("invalid integer: '%s'\n", s);
	return val;
}

static struct ocp_sock *ocp_sock_new(int fd, event_callback_fn cb, int flags)
{
	struct ocp_sock *s;

	s = ocp_sock_free_list;
	if (!s) {
		if (flags & FL_DIE_ON_ERROR)
			die("%s: ran out of ocp_socks\n", __func__);
		return NULL;
	}
	ocp_sock_free_list = s->next;
	memset(s, 0, sizeof(*s));

	if (fd < 0)
		return s;

	s->fd = fd;
	s->ev = event_new(event_base, fd, EV_READ, cb, s);
	if (flags & FL_ACTIVATE)
		event_add(s->ev, NULL);
	return s;
}

static void ocp_sock_del(struct ocp_sock *s)
{
	if (s->state == STATE_DNS) {
		s->state = STATE_DEAD;
		return;
	}
	close(s->fd);
	if (s->tpcb) {
		tcp_arg(s->tpcb, NULL);
		tcp_close(s->tpcb);
	}
	event_free(s->ev);
	memset(s, 0xdd, sizeof(*s));
	s->next = ocp_sock_free_list;
	ocp_sock_free_list = s;
}

/**********************************************************************
 * lwIP TCP<->socket TCP traffic
 **********************************************************************/

/* Called when the local TCP socket has data available (or hung up) */
static void local_data_cb(evutil_socket_t fd, short what, void *ctx)
{
	struct ocp_sock *s = ctx;
	ssize_t len;
	int try_len;
	err_t err;

	try_len = tcp_sndbuf(s->tpcb);
	if (try_len > SOCKBUF_LEN)
		try_len = SOCKBUF_LEN;
	if (!try_len) {
		s->lwip_blocked = 1;
		return;
	}

	len = read(s->fd, s->sockbuf, try_len);
	if (len <= 0) {
		ocp_sock_del(s);
		return;
	}
	err = tcp_write(s->tpcb, s->sockbuf, len, TCP_WRITE_FLAG_COPY);
	if (err == ERR_MEM)
		die("%s: out of memory\n", __func__);
	else if (err != ERR_OK)
		warn("tcp_write returned %d\n", (int)err);

	tcp_output(s->tpcb);
	event_add(s->ev, NULL);
}

/* Called when lwIP has sent data to the VPN */
static err_t sent_cb(void *ctx, struct tcp_pcb *tpcb, u16_t len)
{
	struct ocp_sock *s = ctx;

	if (!s)
		return ERR_OK;

	if (s->lwip_blocked) {
		s->lwip_blocked = 0;
		event_add(s->ev, NULL);
	}

	return ERR_OK;
}

/* Called when lwIP has new TCP data from the VPN */
static err_t recv_cb(void *ctx, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct ocp_sock *s = ctx;
	struct pbuf *first = p;
	int offset;
	ssize_t wlen;

	if (!s)
		return ERR_ABRT;

	if (!p) {
		ocp_sock_del(s);
		return ERR_OK;
	}

	/*
	 * tcp_tmr() will call this function every 250ms with the same pbuf,
	 * if we refused data during the previous attempt.  s->done_len
	 * will reflect the number of bytes we were able to send to the socket
	 * so far (if any).
	 */

	for (offset = s->done_len; p && offset >= p->len; offset -= p->len)
		p = p->next;

	for (; p; p = p->next) {
		int try_len = p->len - offset;

		wlen = write(s->fd, (char *)p->payload + offset, try_len);
		offset = 0;

		if (wlen < 0) {
			ocp_sock_del(s);
			return ERR_ABRT;
		}
		s->done_len += wlen;
		if (wlen < try_len)
			return ERR_WOULDBLOCK;
	}

	/* if we got here, then the whole pbuf is done */
	tcp_recved(tpcb, s->done_len);
	s->done_len = 0;
	pbuf_free(first);

	return ERR_OK;
}

/**********************************************************************
 * SOCKS protocol
 **********************************************************************/

static void socks_reply(struct ocp_sock *s, int rep)
{
	struct socks_reply rsp;

	memset(&rsp, 0, sizeof(rsp));
	rsp.ver = SOCKS_VER;
	rsp.rep = rep;
	rsp.atyp = SOCKS_ATYP_IPV4;

	if (rep == 0 && s->tpcb) {
		rsp.bnd_addr = htonl(s->tpcb->local_ip.addr);
		rsp.bnd_port = htons(s->tpcb->local_port);
	}
	write(s->fd, &rsp, sizeof(rsp));

	if (rep != 0)
		ocp_sock_del(s);
}

static void socks_cmd_cb(evutil_socket_t fd, short what, void *ctx)
{
	struct ocp_sock *s = ctx;
	ssize_t ret;
	struct socks_auth *auth = (void *)s->sockbuf;
	struct socks_req *req = (void *)s->sockbuf;
	ip_addr_t ip;

	if (s->state == STATE_DATA) {
		/* we're done with the SOCKS negotiation so just pass data */
		local_data_cb(fd, what, ctx);
		return;
	}

	ret = read(s->fd, s->sockbuf + s->sock_pos, SOCKBUF_LEN - s->sock_pos);
	if (ret <= 0)
		goto disconnect;
	s->sock_pos += ret;

	if (req->ver != SOCKS_VER)
		goto disconnect;

	if (s->state == STATE_SOCKS_AUTH) {
		if (s->sock_pos <= offsetof(struct socks_auth, n_methods))
			goto req_more;
		else if (s->sock_pos <= auth->n_methods +
					offsetof(struct socks_auth, n_methods))
			goto req_more;

		/* reply: SOCKS5, no auth needed */
		write(s->fd, "\x05\x00", 2);

		s->state = STATE_SOCKS_CMD;
		s->sock_pos = 0;
		goto req_more;
	} else if (s->state == STATE_SOCKS_CMD) {
		/* read cmd, atyp */
		if (s->sock_pos <= offsetof(struct socks_req, atyp))
			goto req_more;
		if (req->cmd != SOCKS_CMD_CONNECT) {
			socks_reply(s, SOCKS_CMDNOTSUPP);
			return;
		}
		if (req->atyp == SOCKS_ATYP_IPV4) {
			if (s->sock_pos < offsetof(struct socks_req, u.ipv4.end))
				goto req_more;
			ip.addr = req->u.ipv4.dst_addr;
			s->rport = ntohs(req->u.ipv4.dst_port);
			start_connection(NULL, &ip, s);
			return;
		} else if (req->atyp == SOCKS_ATYP_DOMAIN) {
			u8_t *name = req->u.fqdn.fqdn_name;
			u16_t namelen = req->u.fqdn.fqdn_len;

			if (s->sock_pos <= offsetof(struct socks_req,
			    u.fqdn.fqdn_len))
				goto req_more;
			if (s->sock_pos <= (offsetof(struct socks_req,
			    u.fqdn.fqdn_len) + req->u.fqdn.fqdn_len))
				goto req_more;
			s->rport = (name[namelen] << 8) | name[namelen + 1];
			name[namelen] = 0;
			start_resolution(s, (char *)name);
			return;
		} else {
			socks_reply(s, SOCKS_ADDRNOTSUPP);
			return;
		}
	}

req_more:
	event_add(s->ev, NULL);
	return;

disconnect:
	ocp_sock_del(s);
}

/**********************************************************************
 * Connection setup
 **********************************************************************/

/* Called on lwIP TCP errors; used to detect connection failure */
static void tcp_err_cb(void *arg, err_t err)
{
	struct ocp_sock *s = arg;

	if (s) {
		s->tpcb = NULL;
		if (s->state == STATE_CONNECTING) {
			if (s->conn_type == CONN_TYPE_SOCKS)
				socks_reply(s, SOCKS_CONNREFUSED);
		} else
			ocp_sock_del(s);
	}
}

/* Called when lwIP tcp_connect() is successful */
static err_t connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	struct ocp_sock *s = arg;

	if (s->conn_type == CONN_TYPE_SOCKS)
		socks_reply(s, SOCKS_OK);

	s->state = STATE_DATA;
	event_add(s->ev, NULL);
	tcp_recv(tpcb, recv_cb);
	tcp_sent(tpcb, sent_cb);

	return ERR_OK;
}

static void start_connection(const char *hostname, ip_addr_t *ipaddr, void *arg)
{
	struct ocp_sock *s = arg;
	struct tcp_pcb *tpcb;
	err_t err;

	/*
	 * We can't abort the DNS lookup, but we can kill the connection when
	 * it returns (if needed)
	 */
	if (s->state == STATE_DEAD) {
		ocp_sock_del(s);
		return;
	}

	if (ipaddr == NULL) {
		/* DNS resolution failed */
		if (s->conn_type == CONN_TYPE_SOCKS)
			socks_reply(s, SOCKS_HOST_UNREACHABLE);
		else
			ocp_sock_del(s);
		return;
	}

	s->state = STATE_CONNECTING;

	tpcb = tcp_new();
	if (!tpcb)
		die("%s: out of memory\n", __func__);
	tcp_nagle_disable(tpcb);
	tcp_arg(tpcb, s);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, tcp_err_cb);
	s->tpcb = tpcb;

	if (keep_intvl) {
		tpcb->so_options |= SOF_KEEPALIVE;
		tpcb->keep_intvl = keep_intvl * 1000;
		tpcb->keep_idle = tpcb->keep_intvl;
	}

	err = tcp_connect(tpcb, ipaddr, s->rport, connect_cb);
	if (err != ERR_OK)
		warn("%s: tcp_connect() returned %d\n", __func__, (int)err);
}

static void start_resolution(struct ocp_sock *s, const char *hostname)
{
	err_t err;

	s->state = STATE_DNS;

	err = dns_gethostbyname(hostname, &s->rhost, start_connection, s);
	if (err == ERR_INPROGRESS)
		return;
	else if (err == ERR_OK)
		start_connection(hostname, &s->rhost, s);
	else
		warn("%s: invalid hostname '%s'\n", __func__, hostname);
}

/* Called upon connection to a local TCP socket */
static void new_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
			struct sockaddr *address, int socklen, void *ctx)
{
	struct ocp_sock *lsock = ctx, *s;

	s = ocp_sock_new(fd, lsock->conn_type == CONN_TYPE_REDIR ?
			 local_data_cb : socks_cmd_cb, 0);
	if (!s) {
		warn("too many connections\n");
		return;
	}

	s->conn_type = lsock->conn_type;
	s->rport = lsock->rport;

	if (s->conn_type == CONN_TYPE_REDIR) {
		s->rport = lsock->rport;
		start_resolution(s, lsock->rhost_name);
	} else {
		s->state = STATE_SOCKS_AUTH;
		event_add(s->ev, NULL);
	}
}

/**********************************************************************
 * lwIP<->VPN traffic
 **********************************************************************/

static void vpn_conn_down(void)
{
	printf("ocproxy: VPN connection has terminated\n");
	event_base_loopbreak(event_base);
}

/* Called when the VPN sends us a raw IP packet destined for lwIP */
static void lwip_data_cb(evutil_socket_t fd, short what, void *ctx)
{
	struct ocp_sock *s = ctx;
	ssize_t len;
	struct pbuf *p;

	len = read(s->fd, s->sockbuf, SOCKBUF_LEN);
	if (len <= 0) {
		/* This might never happen, because s->fd is a DGRAM socket */
		vpn_conn_down();
	}
	if ((p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL)) != NULL) {
		char *bufptr;
		struct pbuf *q;

		bufptr = s->sockbuf;
		q = p;
		while (len > 0) {
			int copy = (len > q->len) ? q->len : len;

			memcpy(q->payload, bufptr, copy);
			len -= copy;
			bufptr += copy;
			q = q->next;
		}
		LINK_STATS_INC(link.recv);
		if (tcpdump_enabled)
			tcpdump(p);
		s->netif->input(p, s->netif);
	} else
		warn("%s: could not allocate pbuf\n", __func__);
	event_add(s->ev, NULL);
}

/* Called when lwIP has data to send up to the VPN */
static err_t lwip_data_out(struct netif *netif, struct pbuf *p, ip_addr_t *ipaddr)
{
	struct ocp_sock *s = netif->state;
	int i = 0, total = 0;
	ssize_t ret;
	struct iovec iov[MAX_IOVEC];

	if (tcpdump_enabled)
		tcpdump(p);

	for (; p; p = p->next) {
		if (i >= MAX_IOVEC) {
			warn("%s: too many chunks, dropping packet\n", __func__);
			return ERR_OK;
		}
		iov[i].iov_base = p->payload;
		iov[i++].iov_len = p->len;
		total += p->len;
	}

	ret = writev(s->fd, iov, i);
	if (ret < 0) {
		if (errno == ECONNREFUSED || errno == ENOTCONN)
			vpn_conn_down();
		else
			LINK_STATS_INC(link.drop);
	} else if (ret != total)
		LINK_STATS_INC(link.lenerr);
	else
		LINK_STATS_INC(link.xmit);

	return ERR_OK;
}

/**********************************************************************
 * Periodic tasks
 **********************************************************************/

static void handle_sig(int sig)
{
	if (sig == SIGHUP)
		got_sighup = 1;
	else if (sig == SIGUSR1)
		got_sigusr1 = 1;
}

static void new_periodic_event(event_callback_fn cb, void *arg, int timeout_ms)
{
	struct timeval tv;
	struct event *ev;

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = 1000 * (timeout_ms % 1000);
	ev = event_new(event_base, -1, EV_PERSIST, cb, arg);
	if (!ev)
		die("can't create new periodic event\n");
	evtimer_add(ev, &tv);
}

static void cb_tcp_tmr(evutil_socket_t fd, short what, void *ctx)
{
	tcp_tmr();
}

static void cb_dns_tmr(evutil_socket_t fd, short what, void *ctx)
{
	dns_tmr();
}

static void cb_housekeeping(evutil_socket_t fd, short what, void *ctx)
{
	int *vpnfd = ctx;

	/*
	 * OpenConnect will ignore 0-byte datagrams if it's alive, but
	 * we'll get ECONNREFUSED if the peer has died.
	 */
	if (write(*vpnfd, vpnfd, 0) < 0 &&
	    (errno == ECONNREFUSED || errno == ENOTCONN))
		vpn_conn_down();

	if (got_sighup)
		vpn_conn_down();

	if (got_sigusr1) {
		LINK_STATS_DISPLAY();
		got_sigusr1 = 0;
	}
}

/**********************************************************************
 * Program initialization
 **********************************************************************/

static err_t init_oc_netif(struct netif *netif)
{
	netif->name[0] = 'u';
	netif->name[1] = 'n';
	netif->output = lwip_data_out;
	return ERR_OK;
}

static struct ocp_sock *new_listener(int port, evconnlistener_cb cb)
{
	struct sockaddr_in sock;
	struct ocp_sock *s;

	memset(&sock, 0, sizeof(sock));
        sock.sin_family = AF_INET;
        sock.sin_port = htons(port);
        sock.sin_addr.s_addr = htonl(allow_remote ? INADDR_ANY : INADDR_LOOPBACK);

	s = ocp_sock_new(-1, NULL, FL_DIE_ON_ERROR);
	s->listener = evconnlistener_new_bind(event_base, cb, s,
		LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
		(struct sockaddr *)&sock, sizeof(sock));
	if (!s->listener)
		die("can't set up listener on port %d/tcp\n", port);

	return s;
}

static void fwd_add(const char *opt)
{
	char *str = xstrdup(opt), *tmp = str, *p;
	int lport;
	struct ocp_sock *s;

	p = strsep(&str, ":");
	if (!str)
		goto bad;
	lport = ocp_atoi(p);

	p = strsep(&str, ":");
	if (!str)
		goto bad;

	s = new_listener(lport, new_conn_cb);
	s->rhost_name = xstrdup(p);
	s->rport = ocp_atoi(str);
	s->conn_type = CONN_TYPE_REDIR;

	if (s->rport <= 0)
		die("Remote port must be a positive integer\n");

	free(tmp);

	return;
bad:
	die("Invalid port forward specifier: '%s'\n", opt);
}

static struct option longopts[] = {
	{ "ip",			1,	NULL,	'I' },
	{ "netmask",		1,	NULL,	'N' },
	{ "gw",			1,	NULL,	'G' },
	{ "mtu",		1,	NULL,	'M' },
	{ "dns",		1,	NULL,	'd' },
	{ "localfw",		1,	NULL,	'L' },
	{ "dynfw",		1,	NULL,	'D' },
	{ "keepalive",		1,	NULL,	'k' },
	{ "allow-remote",	0,	NULL,	'g' },
	{ "verbose",		0,	NULL,	'v' },
	{ "tcpdump",		0,	NULL,	'T' },
	{ NULL }
};

int main(int argc, char **argv)
{
	int opt, i, vpnfd;
	char *str;
	char *ip_str, *netmask_str, *gw_str, *mtu_str, *dns_str;
	ip_addr_t ip, netmask, gw, dns;
	in_port_t socks_port = 0;
	struct ocp_sock *s;
	struct netif netif;

	ocp_sock_free_list = &ocp_sock_pool[0];
	for (i = 1; i < MAX_CONN; i++)
		ocp_sock_pool[i - 1].next = &ocp_sock_pool[i];

	event_base = event_base_new();
	if (!event_base)
		die("can't initialize libevent\n");

	str = getenv("VPNFD");
	if (!str)
		die("VPNFD is not set, aborting\n");
	vpnfd = ocp_atoi(str);
	s = ocp_sock_new(vpnfd, lwip_data_cb, FL_ACTIVATE | FL_DIE_ON_ERROR);

	/* try to set the IP configuration from the environment, first */
	ip_str = getenv("INTERNAL_IP4_ADDRESS");
	netmask_str = getenv("INTERNAL_IP4_NETMASK");
	gw_str = getenv("VPNGATEWAY");
	mtu_str = getenv("INTERNAL_IP4_MTU");
	str = getenv("INTERNAL_IP4_DNS");
	if (str) {
		char *p;

		/* this could contain many addresses; just use the first one */
		dns_str = xstrdup(str);
		p = strchr(dns_str, ' ');
		if (p)
			*p = 0;
	}

	/* override with command line options */
	while ((opt = getopt_long(argc, argv,
				  "I:N:G:M:d:D:k:gL:vT", longopts, NULL)) != -1) {
		switch (opt) {
		case 'I':
			ip_str = optarg;
			break;
		case 'N':
			netmask_str = optarg;
			break;
		case 'G':
			gw_str = optarg;
			break;
		case 'M':
			mtu_str = optarg;
			break;
		case 'd':
			dns_str = optarg;
			break;
		case 'D':
			socks_port = ocp_atoi(optarg);
			break;
		case 'k':
			keep_intvl = ocp_atoi(optarg);
			break;
		case 'g':
			allow_remote = 1;
			break;
		case 'L':
			fwd_add(optarg);
			break;
		case 'v':
			debug_flags = LWIP_DBG_ON | LWIP_DBG_TRACE |
				      LWIP_DBG_STATE | LWIP_DBG_FRESH |
				      LWIP_DBG_HALT;
			break;
		case 'T':
			tcpdump_enabled = 1;
			break;
		default:
			die("unknown option: %c\n", opt);
		}
	}

	if (!ip_str || !netmask_str || !gw_str || !mtu_str)
		die("missing -I, -N, -G, or -M\n");

	if (!ipaddr_aton(ip_str, &ip))
		die("Invalid IP address: '%s'\n", ip_str);
	if (!ipaddr_aton(netmask_str, &netmask))
		die("Invalid netmask: '%s'\n", netmask_str);
	if (!ipaddr_aton(gw_str, &gw))
		die("Invalid gateway: '%s'\n", gw_str);

	/* Debugging help. */
	signal(SIGHUP, handle_sig);
	signal(SIGUSR1, handle_sig);
	signal(SIGPIPE, SIG_IGN);
	setlinebuf(stdout);
	setlinebuf(stderr);

	/* Set up lwIP interface */
	memset(&netif, 0, sizeof(netif));
	s->netif = &netif;

	lwip_init();
	dns_init();

	if (dns_str) {
		if (!ipaddr_aton(dns_str, &dns))
			die("Invalid DNS IP: '%s'\n", dns_str);
		/* this replaces the default opendns server */
		dns_setserver(0, &dns);
	}

	netif_add(&netif, &ip, &netmask, &gw, s, init_oc_netif,
		  ip_input);
	netif.mtu = ocp_atoi(mtu_str);

	netif_set_default(&netif);
	netif_set_up(&netif);

	if (socks_port) {
		s = new_listener(socks_port, new_conn_cb);
		s->conn_type = CONN_TYPE_SOCKS;
	}

	if (tcpdump_enabled)
		tcpdump_init();

	new_periodic_event(cb_tcp_tmr, NULL, 250);
	new_periodic_event(cb_dns_tmr, NULL, 1000);
	new_periodic_event(cb_housekeeping, &vpnfd, 1000);

	event_base_dispatch(event_base);

	return 0;
}
