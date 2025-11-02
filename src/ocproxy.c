/*
 * Copyright (c) 2012-2014 David Edmondson <dme@dme.org>
 * Copyright (c) 2012-2014 Kevin Cernekee <cernekee@gmail.com>
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
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
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>

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

enum {
	STATE_NEW		= 0,
	STATE_SOCKS_AUTH,
	STATE_SOCKS_CMD,
	STATE_HTTP_REQ,
	STATE_HTTP_HEADERS,
	STATE_HTTP_RELAY,
	STATE_DNS,
	STATE_CONNECTING,
	STATE_DATA,
	STATE_DEAD,
	STATE_MAX
};

#define CONN_TYPE_REDIR		0
#define CONN_TYPE_SOCKS		1
#define CONN_TYPE_HTTP		2

#define SOCKBUF_LEN		2048

#define FL_ACTIVATE		1
#define FL_DIE_ON_ERROR		2

#define MAX_IOVEC		128
#define MAX_CONN		1024

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

	/* for all listeners */
	int lport;
	char *bind_addr;
	evconnlistener_cb listen_cb;

	/* for port forwarding */
	char *rhost_name;
	ip_addr_t rhost;
	int rport;

	/* for lwip_data_cb() */
	struct netif *netif;

	/* for HTTP proxy */
	int http_relay_mode;
	char *http_request;
	int http_request_len;
	int http_request_capacity;
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
static struct ocp_sock *ocp_sock_bind_list;
static int ocp_sock_used;
static int ocp_sock_max;

/* nonstatic debug cmd option, exported in lwipopts.h */
unsigned char debug_flags = 0;

static int allow_remote;
static int tcpdump_enabled;
static int keep_intvl;
static int got_sighup;
static int got_sigusr1;
static char *dns_domain;
static FILE *logfile;
static char *logfile_path;

static void start_connection(struct ocp_sock *s, ip_addr_t *ipaddr);
static void start_resolution(struct ocp_sock *s, const char *hostname);

/**********************************************************************
 * Utility functions / libevent wrappers
 **********************************************************************/

/* Portable strcasestr for systems that don't have it */
#ifndef __APPLE__
#ifndef __GLIBC__
static char *strcasestr(const char *haystack, const char *needle)
{
	size_t needle_len = strlen(needle);
	if (needle_len == 0)
		return (char *)haystack;
	
	for (; *haystack; haystack++) {
		if (strncasecmp(haystack, needle, needle_len) == 0)
			return (char *)haystack;
	}
	return NULL;
}
#endif
#endif

static void die(const char *fmt, ...) __attribute__((format(printf, 1, 2))) __attribute__((noreturn));
static void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

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

/* Get current timestamp in ISO 8601 format */
static void get_timestamp(char *buf, size_t len)
{
	time_t now;
	struct tm *tm_info;

	time(&now);
	tm_info = localtime(&now);
	strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Log connection request */
static void log_request(const char *protocol, const char *destination, int port)
{
	char timestamp[64];

	if (!logfile)
		return;

	get_timestamp(timestamp, sizeof(timestamp));
	fprintf(logfile, "[%s] %s -> %s:%d\n", timestamp, protocol, destination, port);
	fflush(logfile);
}

/* Log HTTP/HTTPS request with full URL */
static void log_http_request(const char *method, const char *url, const char *host, int port)
{
	char timestamp[64];
	char full_url[2048];

	if (!logfile)
		return;

	get_timestamp(timestamp, sizeof(timestamp));
	
	/* Construct full URL */
	if (strcasecmp(method, "CONNECT") == 0) {
		/* HTTPS tunneling */
		snprintf(full_url, sizeof(full_url), "https://%s:%d/", host, port);
		fprintf(logfile, "[%s] HTTPS CONNECT -> %s\n", timestamp, full_url);
	} else {
		/* Regular HTTP request */
		if (strncasecmp(url, "http://", 7) == 0) {
			/* Absolute URL */
			fprintf(logfile, "[%s] HTTP %s -> %s\n", timestamp, method, url);
		} else {
			/* Relative URL - construct full URL */
			const char *scheme = (port == 443) ? "https" : "http";
			if (port == 80 || port == 443) {
				snprintf(full_url, sizeof(full_url), "%s://%s%s", scheme, host, url);
			} else {
				snprintf(full_url, sizeof(full_url), "%s://%s:%d%s", scheme, host, port, url);
			}
			fprintf(logfile, "[%s] HTTP %s -> %s\n", timestamp, method, full_url);
		}
	}
	fflush(logfile);
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

	ocp_sock_used++;
	if (ocp_sock_used > ocp_sock_max)
		ocp_sock_max = ocp_sock_used;

	s->next = ocp_sock_bind_list;
	ocp_sock_bind_list = s;

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
	if (s->http_request) {
		free(s->http_request);
		s->http_request = NULL;
	}
	memset(s, 0xdd, sizeof(*s));
	s->next = ocp_sock_free_list;
	ocp_sock_free_list = s;
	ocp_sock_used--;
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
	if (!try_len || tcp_sndqueuelen(s->tpcb) > (TCP_SND_QUEUELEN/2)) {
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
		tcp_recved(tpcb, wlen);
		if (wlen < try_len)
			return ERR_WOULDBLOCK;
	}

	/* if we got here, then the whole pbuf is done */
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
	if (write(s->fd, &rsp, sizeof(rsp)) != sizeof(rsp))
		rep = -1;

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
		if (write(s->fd, "\x05\x00", 2) != 2)
			goto disconnect;

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
			
			/* Log SOCKS connection to IPv4 address */
			char ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &ip.addr, ip_str, sizeof(ip_str));
			log_request("SOCKS5", ip_str, s->rport);
			
			start_connection(s, &ip);
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
			
			/* Log SOCKS connection to domain */
			log_request("SOCKS5", (char *)name, s->rport);
			
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
 * HTTP proxy protocol
 **********************************************************************/

static void http_send_error(struct ocp_sock *s, const char *status, const char *msg)
{
	char buf[512];
	int len;

	len = snprintf(buf, sizeof(buf),
		       "HTTP/1.1 %s\r\n"
		       "Content-Type: text/plain\r\n"
		       "Connection: close\r\n"
		       "\r\n"
		       "%s\r\n", status, msg);

	if (write(s->fd, buf, len) != len)
		; /* ignore error */
	ocp_sock_del(s);
}

static void http_send_connect_ok(struct ocp_sock *s)
{
	const char *response = "HTTP/1.1 200 Connection established\r\n\r\n";
	int len = strlen(response);

	if (write(s->fd, response, len) != len) {
		ocp_sock_del(s);
		return;
	}

	s->state = STATE_DATA;
	event_add(s->ev, NULL);
	tcp_recv(s->tpcb, recv_cb);
	tcp_sent(s->tpcb, sent_cb);
}

/* Called when lwIP has data from target for HTTP relay mode */
static err_t http_relay_recv_cb(void *ctx, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
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

	/* Send response data back to client */
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
		tcp_recved(tpcb, wlen);
		if (wlen < try_len)
			return ERR_WOULDBLOCK;
	}

	s->done_len = 0;
	pbuf_free(first);

	return ERR_OK;
}

/* Called when client sends data in HTTP relay mode */
static void http_relay_local_cb(evutil_socket_t fd, short what, void *ctx)
{
	struct ocp_sock *s = ctx;
	ssize_t len;
	int try_len;
	err_t err;

	try_len = tcp_sndbuf(s->tpcb);
	if (try_len > SOCKBUF_LEN)
		try_len = SOCKBUF_LEN;
	if (!try_len || tcp_sndqueuelen(s->tpcb) > (TCP_SND_QUEUELEN/2)) {
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

/* Called when lwIP tcp_connect() is successful for HTTP CONNECT */
static err_t http_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	struct ocp_sock *s = arg;

	http_send_connect_ok(s);
	return ERR_OK;
}

/* Called when lwIP tcp_connect() is successful for HTTP relay mode */
static err_t http_relay_connect_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	struct ocp_sock *s = arg;
	err_t write_err;

	if (!s || !s->http_request)
		return ERR_ABRT;

	/* Connection established, send the HTTP request to target server */
	write_err = tcp_write(tpcb, s->http_request, s->http_request_len, TCP_WRITE_FLAG_COPY);
	if (write_err == ERR_MEM) {
		warn("%s: out of memory sending HTTP request\n", __func__);
		ocp_sock_del(s);
		return ERR_ABRT;
	} else if (write_err != ERR_OK) {
		warn("tcp_write returned %d\n", (int)write_err);
		ocp_sock_del(s);
		return ERR_ABRT;
	}

	tcp_output(tpcb);

	/* Free the request buffer */
	free(s->http_request);
	s->http_request = NULL;
	s->http_request_len = 0;
	s->http_request_capacity = 0;

	/* Now relay responses and further requests */
	s->state = STATE_HTTP_RELAY;
	event_del(s->ev);
	event_free(s->ev);
	s->ev = event_new(event_base, s->fd, EV_READ, http_relay_local_cb, s);
	event_add(s->ev, NULL);
	tcp_recv(tpcb, http_relay_recv_cb);
	tcp_sent(tpcb, sent_cb);

	return ERR_OK;
}

static void http_req_cb(evutil_socket_t fd, short what, void *ctx)
{
	struct ocp_sock *s = ctx;
	ssize_t ret;
	char *headers_end, *method, *url, *host_start, *port_str, *path_start;
	char hostname[256];
	char *host_header;
	ip_addr_t ip;

	if (s->state == STATE_DATA) {
		/* HTTP CONNECT tunnel established, pass data */
		local_data_cb(fd, what, ctx);
		return;
	}

	if (s->state == STATE_HTTP_RELAY) {
		/* HTTP relay mode active */
		http_relay_local_cb(fd, what, ctx);
		return;
	}

	ret = read(s->fd, s->sockbuf + s->sock_pos, SOCKBUF_LEN - s->sock_pos - 1);
	if (ret <= 0)
		goto disconnect;

	s->sock_pos += ret;
	s->sockbuf[s->sock_pos] = '\0';

	/* Look for end of HTTP headers */
	headers_end = strstr(s->sockbuf, "\r\n\r\n");
	if (!headers_end) {
		if (s->sock_pos >= SOCKBUF_LEN - 1) {
			http_send_error(s, "400 Bad Request", "Request too long");
			return;
		}
		event_add(s->ev, NULL);
		return;
	}

	/* We have complete headers */
	char *request_copy = xstrdup(s->sockbuf);
	char *line_end = strstr(request_copy, "\r\n");
	if (!line_end) {
		free(request_copy);
		http_send_error(s, "400 Bad Request", "Invalid request");
		return;
	}
	*line_end = '\0';

	/* Parse request: METHOD URL HTTP/1.x */
	method = request_copy;
	url = strchr(method, ' ');
	if (!url) {
		free(request_copy);
		http_send_error(s, "400 Bad Request", "Invalid request");
		return;
	}
	*url++ = '\0';

	while (*url == ' ')
		url++;

	char *http_ver = strchr(url, ' ');
	if (!http_ver) {
		free(request_copy);
		http_send_error(s, "400 Bad Request", "Invalid request");
		return;
	}
	*http_ver++ = '\0';

	/* Check if this is a CONNECT request */
	if (strcasecmp(method, "CONNECT") == 0) {
		host_start = url;
	} else {
		/* Regular HTTP request (GET, POST, PUT, DELETE, etc.) */
		s->http_relay_mode = 1;

		/* Parse URL to extract host and port */
		if (strncasecmp(url, "http://", 7) == 0) {
			/* Absolute URL format: http://host:port/path */
			host_start = url + 7;
			path_start = strchr(host_start, '/');
			if (path_start) {
				*path_start = '\0';
			}
		} else {
			/* Relative URL - need to find Host header */
			host_header = strcasestr(s->sockbuf, "\r\nHost:");
			if (!host_header) {
				free(request_copy);
				http_send_error(s, "400 Bad Request", "Missing Host header");
				return;
			}
			host_header += 7; /* skip "\r\nHost:" */
			while (*host_header == ' ' || *host_header == '\t')
				host_header++;
			
			char *host_end = strstr(host_header, "\r\n");
			if (!host_end) {
				free(request_copy);
				http_send_error(s, "400 Bad Request", "Invalid Host header");
				return;
			}
			
			int host_len = host_end - host_header;
			if (host_len >= sizeof(hostname)) {
				free(request_copy);
				http_send_error(s, "400 Bad Request", "Host header too long");
				return;
			}
			
			memcpy(hostname, host_header, host_len);
			hostname[host_len] = '\0';
			
			/* Parse hostname:port */
			port_str = strchr(hostname, ':');
			if (port_str) {
				*port_str++ = '\0';
				s->rport = ocp_atoi(port_str);
			} else {
				s->rport = 80; /* default HTTP port */
			}

			/* Store the entire HTTP request to forward */
			int req_len = headers_end - s->sockbuf + 4;
			s->http_request = malloc(req_len + 1);
			if (!s->http_request) {
				free(request_copy);
				http_send_error(s, "500 Internal Server Error", "Out of memory");
				return;
			}
			memcpy(s->http_request, s->sockbuf, req_len);
			s->http_request[req_len] = '\0';
			s->http_request_len = req_len;
			s->http_request_capacity = req_len + 1;

			/* Log the HTTP request - url already contains the full path with query params */
			log_http_request(method, url, hostname, s->rport);
			
			free(request_copy);

			/* Check if it's an IP or hostname */
			if (ipaddr_aton(hostname, &ip)) {
				start_connection(s, &ip);
			} else {
				start_resolution(s, hostname);
			}
			return;
		}
		
		/* For absolute URLs, extract the hostname for connection */
		char *url_copy = xstrdup(url);
		host_start = url_copy + 7; /* skip "http://" */
		path_start = strchr(host_start, '/');
		if (path_start)
			*path_start = '\0';
		
		strncpy(hostname, host_start, sizeof(hostname) - 1);
		hostname[sizeof(hostname) - 1] = '\0';
		free(url_copy);
		
		/* Parse hostname:port from the extracted host */
		port_str = strchr(hostname, ':');
		if (port_str) {
			*port_str++ = '\0';
			s->rport = ocp_atoi(port_str);
		} else {
			s->rport = 80;
		}

		/* Store the entire HTTP request to forward */
		int req_len = headers_end - s->sockbuf + 4;
		s->http_request = malloc(req_len + 1);
		if (!s->http_request) {
			free(request_copy);
			http_send_error(s, "500 Internal Server Error", "Out of memory");
			return;
		}
		memcpy(s->http_request, s->sockbuf, req_len);
		s->http_request[req_len] = '\0';
		s->http_request_len = req_len;
		s->http_request_capacity = req_len + 1;

		/* Log the HTTP request - url contains the full absolute URL */
		log_http_request(method, url, hostname, s->rport);
		
		free(request_copy);

		/* Check if it's an IP or hostname */
		if (ipaddr_aton(hostname, &ip)) {
			start_connection(s, &ip);
		} else {
			start_resolution(s, hostname);
		}
		return;
	}

	/* This code handles CONNECT requests only (is_connect == 1) */
	/* Parse hostname:port from CONNECT target */
	port_str = strchr(host_start, ':');
	if (port_str) {
		*port_str++ = '\0';
		s->rport = ocp_atoi(port_str);
	} else {
		s->rport = 443; /* default HTTPS port for CONNECT */
	}

	if (s->rport <= 0 || s->rport > 65535) {
		free(request_copy);
		http_send_error(s, "400 Bad Request", "Invalid port");
		return;
	}

	strncpy(hostname, host_start, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = '\0';

	/* Log the HTTPS CONNECT request */
	log_http_request(method, url, hostname, s->rport);

	free(request_copy);

	/* Check if it's an IP address or needs DNS resolution */
	if (ipaddr_aton(hostname, &ip)) {
		start_connection(s, &ip);
	} else {
		start_resolution(s, hostname);
	}
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
		if (s->state == STATE_CONNECTING &&
		    s->conn_type == CONN_TYPE_SOCKS)
			socks_reply(s, SOCKS_CONNREFUSED);
		else if (s->state == STATE_CONNECTING &&
			 s->conn_type == CONN_TYPE_HTTP)
			http_send_error(s, "502 Bad Gateway", "Connection failed");
		else
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

static void start_connection(struct ocp_sock *s, ip_addr_t *ipaddr)
{
	struct tcp_pcb *tpcb;
	err_t err;
	tcp_connected_fn connected_cb;

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

	/* Use appropriate callback based on connection type and mode */
	if (s->conn_type == CONN_TYPE_HTTP) {
		if (s->http_relay_mode)
			connected_cb = http_relay_connect_cb;
		else
			connected_cb = http_connect_cb;
	} else {
		connected_cb = connect_cb;
	}

	err = tcp_connect(tpcb, ipaddr, s->rport, connected_cb);
	if (err != ERR_OK)
		warn("%s: tcp_connect() returned %d\n", __func__, (int)err);
}

static void finish_resolution(const char *hostname, ip_addr_t *ipaddr, void *arg)
{
	struct ocp_sock *s = arg;

	/*
	 * We can't abort the DNS lookup, but we can kill the connection when
	 * it returns (if needed)
	 */
	if (s->state == STATE_DEAD) {
		ocp_sock_del(s);
		return;
	}

	if (ipaddr) {
		/* success */
		start_connection(s, ipaddr);
		return;
	}

	/* DNS resolution failed */
	if (s->conn_type == CONN_TYPE_SOCKS)
		socks_reply(s, SOCKS_HOST_UNREACHABLE);
	else if (s->conn_type == CONN_TYPE_HTTP)
		http_send_error(s, "502 Bad Gateway", "Host not found");
	else
		ocp_sock_del(s);
}

static void enqueue_dns_req(struct ocp_sock *s, const char *hostname,
			    const char *domain, dns_found_callback found)
{
	err_t err;

	if (!domain)
		err = dns_gethostbyname(hostname, &s->rhost, found, s);
	else {
		/* sockbuf is just scratch space */
		snprintf(s->sockbuf, SOCKBUF_LEN, "%s.%s", hostname, dns_domain);
		err = dns_gethostbyname(s->sockbuf, &s->rhost, found, s);
	}

	if (err == ERR_INPROGRESS)
		return;
	else if (err == ERR_OK)
		start_connection(s, &s->rhost);
	else if (err == ERR_MEM) {
		warn("%s: DNS table full, aborting lookup\n", __func__);
		found(hostname, NULL, s);
	} else {
		warn("%s: invalid hostname '%s'\n", __func__, hostname);
		found(hostname, NULL, s);
	}
}

static void retry_resolution(const char *hostname, ip_addr_t *ipaddr, void *arg)
{
	struct ocp_sock *s = arg;

	/* first attempt at DNS resolution succeeded */
	if (ipaddr != NULL || !dns_domain) {
		finish_resolution(hostname, ipaddr, arg);
		return;
	}

	/* no dice; try again with <hostname>.<dns_domain> */
	enqueue_dns_req(s, hostname, dns_domain, finish_resolution);
}

static void start_resolution(struct ocp_sock *s, const char *hostname)
{
	s->state = STATE_DNS;

	/*
	 * Looking up an unqualified hostname can take a few seconds
	 * to time out, so just look up the FQDN right away if it's
	 * obvious.
	 */
	if (strchr(hostname, '.'))
		enqueue_dns_req(s, hostname, NULL, retry_resolution);
	else
		enqueue_dns_req(s, hostname, dns_domain, finish_resolution);
}

/* Called upon connection to a local TCP socket */
static void new_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
			struct sockaddr *address, int socklen, void *ctx)
{
	struct ocp_sock *lsock = ctx, *s;
	event_callback_fn cb;

	/* Select the appropriate callback based on connection type */
	if (lsock->conn_type == CONN_TYPE_REDIR)
		cb = local_data_cb;
	else if (lsock->conn_type == CONN_TYPE_HTTP)
		cb = http_req_cb;
	else
		cb = socks_cmd_cb;

	s = ocp_sock_new(fd, cb, 0);
	if (!s) {
		warn("too many connections\n");
		return;
	}

	s->conn_type = lsock->conn_type;
	s->rport = lsock->rport;

	if (s->conn_type == CONN_TYPE_REDIR) {
		s->rport = lsock->rport;
		
		/* Log port forwarding connection */
		log_request("PORT-FWD", lsock->rhost_name, s->rport);
		
		start_resolution(s, lsock->rhost_name);
	} else if (s->conn_type == CONN_TYPE_HTTP) {
		s->state = STATE_HTTP_REQ;
		event_add(s->ev, NULL);
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
	else if (got_sighup)
		vpn_conn_down();

	if (got_sigusr1) {
		LINK_STATS_DISPLAY();
		MEM_STATS_DISPLAY();
		printf("open connections: %d / %d, max %d\n",
		       ocp_sock_used, MAX_CONN, ocp_sock_max);
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

static void bind_all_listeners(void)
{
	struct ocp_sock *s;
	struct sockaddr_in sock;

	for (s = ocp_sock_bind_list; s; s = s->next) {
		if (!s->listen_cb)
			continue;
		if (s->lport < 1 || s->lport > 65535)
			die("invalid port number: %d\n", s->lport);

		memset(&sock, 0, sizeof(sock));
		sock.sin_port = htons(s->lport);
		sock.sin_family = AF_INET;

		if (s->bind_addr) {
			/*
			 * TODO: support IPv6 and multiple listening sockets
			 * per hostname
			 */
			if (!inet_aton(s->bind_addr, &sock.sin_addr))
				die("can't parse IP: '%s'\n", s->bind_addr);

		} else {
			sock.sin_addr.s_addr = htonl(allow_remote ?
				INADDR_ANY : INADDR_LOOPBACK);
		}

		s->listener = evconnlistener_new_bind(event_base, s->listen_cb,
			s, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
			(struct sockaddr *)&sock, sizeof(sock));
		if (!s->listener)
			die("can't set up listener on port %d/tcp\n", s->lport);
	}
}

static struct ocp_sock *new_listener(int port, evconnlistener_cb cb)
{
	struct ocp_sock *s;

	s = ocp_sock_new(-1, NULL, FL_DIE_ON_ERROR);
	s->lport = port;
	s->listen_cb = cb;

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

static struct ocp_sock *dyn_fwd(const char *arg)
{
	struct ocp_sock *s;
	const char *sep = strrchr(arg, ':');

	if (sep) {
		/* <addr>:<port> format */
		s = new_listener(ocp_atoi(sep + 1), new_conn_cb);
		s->bind_addr = xstrdup(arg);
		s->bind_addr[sep - arg] = 0;
	} else {
		/* <port> only */
		s = new_listener(ocp_atoi(arg), new_conn_cb);
	}

	s->conn_type = CONN_TYPE_SOCKS;
	return s;
}

static struct ocp_sock *http_proxy(const char *arg)
{
	struct ocp_sock *s;
	const char *sep = strrchr(arg, ':');

	if (sep) {
		/* <addr>:<port> format */
		s = new_listener(ocp_atoi(sep + 1), new_conn_cb);
		s->bind_addr = xstrdup(arg);
		s->bind_addr[sep - arg] = 0;
	} else {
		/* <port> only */
		s = new_listener(ocp_atoi(arg), new_conn_cb);
	}

	s->conn_type = CONN_TYPE_HTTP;
	return s;
}

static void print_help(const char *progname)
{
	printf("Usage: %s [OPTIONS]\n\n", progname);
	printf("ocproxy - Cisco AnyConnect compatible proxy server\n\n");
	printf("Options:\n");
	printf("  -I, --ip ADDRESS        Set the IP address for the TUN interface\n");
	printf("  -M, --mtu SIZE          Set the MTU size\n");
	printf("  -d, --dns ADDRESS       Set the DNS server address\n");
	printf("  -o, --domain DOMAIN     Set the DNS domain for unqualified hostnames\n");
	printf("  -L, --localfw SPEC      Add local port forward (format: localport:remotehost:remoteport)\n");
	printf("  -D, --dynfw PORT        Enable SOCKS5 proxy on specified port (format: [addr:]port)\n");
	printf("  -H, --httpproxy PORT    Enable HTTP proxy on specified port (format: [addr:]port)\n");
	printf("  -k, --keepalive SEC     Set TCP keepalive interval in seconds\n");
	printf("  -l, --logfile PATH      Log all connections to specified file\n");
	printf("  -g, --allow-remote      Allow remote connections (default: localhost only)\n");
	printf("  -v, --verbose           Enable verbose debug output\n");
	printf("  -T, --tcpdump           Enable tcpdump-style packet logging\n");
	printf("  -h, --help              Display this help message and exit\n");
	printf("\n");
	printf("Environment variables:\n");
	printf("  VPNFD                   File descriptor for VPN connection (required)\n");
	printf("  INTERNAL_IP4_ADDRESS    IPv4 address (can be overridden with -I)\n");
	printf("  INTERNAL_IP4_MTU        MTU size (can be overridden with -M)\n");
	printf("  INTERNAL_IP4_DNS        DNS server address (can be overridden with -d)\n");
	printf("  CISCO_DEF_DOMAIN        DNS domain (can be overridden with -o)\n");
	printf("\n");
	printf("Examples:\n");
	printf("  # SOCKS5 proxy on port 1080\n");
	printf("  %s -D 1080\n\n", progname);
	printf("  # HTTP proxy on port 8080 with logging\n");
	printf("  %s -H 8080 -l /var/log/ocproxy.log\n\n", progname);
	printf("  # Port forward local 2222 to remote ssh server\n");
	printf("  %s -L 2222:server.example.com:22\n\n", progname);
}

static struct option longopts[] = {
	{ "ip",			1,	NULL,	'I' },
	{ "mtu",		1,	NULL,	'M' },
	{ "dns",		1,	NULL,	'd' },
	{ "domain",		1,	NULL,	'o' },
	{ "localfw",		1,	NULL,	'L' },
	{ "dynfw",		1,	NULL,	'D' },
	{ "httpproxy",		1,	NULL,	'H' },
	{ "keepalive",		1,	NULL,	'k' },
	{ "logfile",		1,	NULL,	'l' },
	{ "allow-remote",	0,	NULL,	'g' },
	{ "verbose",		0,	NULL,	'v' },
	{ "tcpdump",		0,	NULL,	'T' },
	{ "help",		0,	NULL,	'h' },
	{ NULL }
};

int main(int argc, char **argv)
{
	int opt, i, vpnfd = -1;
	char *str;
	char *ip_str, *mtu_str, *dns_str;
	ip_addr_t ip, netmask, gw, dns;
	struct ocp_sock *s;
	struct netif netif;

	ip_str = mtu_str = dns_str = NULL;

	/* Check for help option first, before any initialization */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help(argv[0]);
			exit(0);
		}
	}

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

	/* try to set the IP configuration from the environment, first */
	ip_str = getenv("INTERNAL_IP4_ADDRESS");
	mtu_str = getenv("INTERNAL_IP4_MTU");

	dns_domain = getenv("CISCO_DEF_DOMAIN");
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
				  "I:M:d:o:D:H:k:l:gL:vTh", longopts, NULL)) != -1) {
		switch (opt) {
		case 'I':
			ip_str = optarg;
			break;
		case 'M':
			mtu_str = optarg;
			break;
		case 'd':
			dns_str = optarg;
			break;
		case 'o':
			dns_domain = optarg;
			break;
		case 'D':
			s = dyn_fwd(optarg);
			break;
		case 'H':
			s = http_proxy(optarg);
			break;
		case 'k':
			keep_intvl = ocp_atoi(optarg);
			break;
		case 'l':
			logfile_path = xstrdup(optarg);
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
		case 'h':
			print_help(argv[0]);
			exit(0);
		default:
			print_help(argv[0]);
			exit(1);
		}
	}

	if (!ip_str || !mtu_str)
		die("missing -I or -M\n");

	if (!ipaddr_aton(ip_str, &ip))
		die("Invalid IP address: '%s'\n", ip_str);

	/* Open log file if specified */
	if (logfile_path) {
		logfile = fopen(logfile_path, "a");
		if (!logfile) {
			warn("Failed to open log file '%s': %s\n", 
			     logfile_path, strerror(errno));
			logfile_path = NULL;
		} else {
			/* Write log header */
			char timestamp[64];
			get_timestamp(timestamp, sizeof(timestamp));
			fprintf(logfile, "\n=== ocproxy started at %s ===\n", timestamp);
			fflush(logfile);
		}
	}

	/* Debugging help. */
	signal(SIGHUP, handle_sig);
	signal(SIGUSR1, handle_sig);
	signal(SIGPIPE, SIG_IGN);
	setlinebuf(stdout);
	setlinebuf(stderr);

	/* Set up lwIP interface */
	s = ocp_sock_new(vpnfd, lwip_data_cb, FL_ACTIVATE | FL_DIE_ON_ERROR);
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

	ip_addr_set_zero(&netmask);
	ip_addr_set_zero(&gw);
	netif_add(&netif, &ip, &netmask, &gw, s, init_oc_netif, ip_input);
	netif.mtu = ocp_atoi(mtu_str);

	netif_set_default(&netif);
	netif_set_up(&netif);

	/* bind after all options have been parsed (especially -g) */
	bind_all_listeners();

	if (tcpdump_enabled)
		tcpdump_init();

	new_periodic_event(cb_tcp_tmr, NULL, 250);
	new_periodic_event(cb_dns_tmr, NULL, 1000);
	new_periodic_event(cb_housekeeping, &vpnfd, 1000);

	event_base_dispatch(event_base);

	/* Close log file on exit */
	if (logfile) {
		char timestamp[64];
		get_timestamp(timestamp, sizeof(timestamp));
		fprintf(logfile, "=== ocproxy stopped at %s ===\n\n", timestamp);
		fclose(logfile);
		logfile = NULL;
	}

	return 0;
}
