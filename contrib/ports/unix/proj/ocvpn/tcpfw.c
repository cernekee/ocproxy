#include <stdio.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"

#include "tcpfw.h"

#ifndef TCPFW_DEBUG
#define TCPFW_DEBUG LWIP_DBG_OFF
#endif /* TCPFW_DEBUG */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif /* MAXHOSTNAMELEN */

#define THREAD_NAMELEN 64

struct tcpfwc;

typedef struct tcpfw {
	char name[THREAD_NAMELEN];
	in_port_t lport;
	const char *rhost;
	in_port_t rport;
	int listen;
	int (*acceptor)(struct tcpfwc *);
} tcpfw_t;

typedef struct tcpfwc {
	char l2r[THREAD_NAMELEN];
	char r2l[THREAD_NAMELEN];
	tcpfw_t *p;
	int local;
	struct netconn *remote;
	int refcnt;
} tcpfwc_t;

static void tcpfw_listen(void *);
static void tcpfw_l2r(void *);
static void tcpfw_r2l(void *);

static void
i_tcpfw_init(in_port_t lport, const char *rhost, in_port_t rport, int (*acceptor)(tcpfwc_t *))
{
	tcpfw_t *tcpfw;
	struct sockaddr_in sin;
	int on;

	tcpfw = (tcpfw_t *)malloc(sizeof (*tcpfw));
	memset(tcpfw, 0, sizeof (*tcpfw));

	tcpfw->lport = lport;
	tcpfw->rhost = rhost;
	tcpfw->rport = rport;
	tcpfw->acceptor = acceptor;

	tcpfw->listen = socket(AF_INET, SOCK_STREAM, 0);
	if (tcpfw->listen < 0) {
		fprintf(stderr, "i_tcpfw_init: lport %d: ", lport);
		perror("socket");
		free(tcpfw);
		return;
	}

	snprintf(tcpfw->name, THREAD_NAMELEN-1, "tcpfw_listen_port:%d_fd:%d", lport, tcpfw->listen);

	on = 1;
	if (setsockopt(tcpfw->listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on)) < 0) {
		fprintf(stderr, "i_tcpfw_init: lport %d: ", lport);
		perror("setsockopt");
		close(tcpfw->listen);
		free(tcpfw);
		return;
	}

	bzero(&sin, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(tcpfw->lport);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(tcpfw->listen, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
		fprintf(stderr, "i_tcpfw_init: lport %d: ", lport);
		perror("bind");
		close(tcpfw->listen);
		free(tcpfw);
		return;
	}

	if (listen(tcpfw->listen, 5) < 0) {
		fprintf(stderr, "i_tcpfw_init: lport %d: ", lport);
		perror("listen");
		close(tcpfw->listen);
		free(tcpfw);
		return;
	}

	sys_thread_new(tcpfw->name, tcpfw_listen, tcpfw,
		       DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
}

static void
tcpfw_listen(void *arg)
{
	tcpfw_t *tcpfw = (tcpfw_t *)arg;
	int fd;
	struct sockaddr_in sin;
	socklen_t alen = sizeof (sin);

	LWIP_DEBUGF(TCPFW_DEBUG, ("tcpfw_listen: listening on port %d ", tcpfw->lport));
	if (tcpfw->rhost == NULL) {
		LWIP_DEBUGF(TCPFW_DEBUG, ("(socks).\n"));
	} else {
		LWIP_DEBUGF(TCPFW_DEBUG, ("(-> %s:%d).\n", tcpfw->rhost, tcpfw->rport));
	}

	while ((fd = accept(tcpfw->listen, (struct sockaddr *)&sin, &alen)) >= 0) {
		tcpfwc_t *c;

		c = (tcpfwc_t *)malloc(sizeof (*c));
		memset(c, 0, sizeof (*c));

		snprintf(c->l2r, THREAD_NAMELEN-1, "tcpfw_l2r_port:%d_fd:%d", tcpfw->lport, fd);
		snprintf(c->r2l, THREAD_NAMELEN-1, "tcpfw_r2l_port:%d_fd:%d", tcpfw->lport, fd);
		c->refcnt = 2;
		c->p = tcpfw;
		c->local = fd;
		c->remote = netconn_new(NETCONN_TCP);

		if (c->p->acceptor(c) < 0)
			free(c);
	}
}

static int
tcpfw_adjust_refcnt(tcpfwc_t *c, int adj_val)
{
	int ret;

	sys_prot_t pval = sys_arch_protect();
	c->refcnt += adj_val;
	ret = c->refcnt;
	sys_arch_unprotect(pval);

	return ret;
}

static void
tcpfw_l2r(void *arg)
{
	tcpfwc_t *c = (tcpfwc_t *)arg;

	LWIP_DEBUGF(TCPFW_DEBUG, ("tcpfw_l2r: %s starts.\n", c->l2r));

	while (tcpfw_adjust_refcnt(c, 0) == 2) {
		char buf[2048];
		int n;
		struct timeval tv = { 0, 100000 };
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(c->local, &fds);
		if (select(c->local + 1, &fds, NULL, NULL, &tv) == 0)
			continue;

		n = read(c->local, buf, 2048);
		if (n <= 0)
			break;

		netconn_write(c->remote, buf, n, NETCONN_COPY);
	}

	/* kill remote if the local peer closed the connection first */
	netconn_close(c->remote);
	if (tcpfw_adjust_refcnt(c, -1) == 0)
		free(c);
}

static void
tcpfw_r2l(void *arg)
{
	tcpfwc_t *c = (tcpfwc_t *)arg;

	LWIP_DEBUGF(TCPFW_DEBUG, ("tcpfw_r2l: %s starts.\n", c->r2l));

	c->remote->recv_timeout = 100;

	while (tcpfw_adjust_refcnt(c, 0) == 2) {
		err_t err;
		struct netbuf *nbuf;

		err = netconn_recv(c->remote, &nbuf);

		if (err == ERR_TIMEOUT)
			continue;

		if (err != ERR_OK)
			break;

		do {
			char *buf;
			u16_t len;

			netbuf_data(nbuf, (void **)&buf, &len);
			write(c->local, buf, len);
		} while (netbuf_next(nbuf) != -1);

		netbuf_delete(nbuf);
	}

	/* kill local if the remote peer closed the connection first */
	close(c->local);
	if (tcpfw_adjust_refcnt(c, -1) == 0)
		free(c);
}

static int
tcpfw_acceptor(tcpfwc_t *c)
{
	err_t err;
	ip_addr_t rhost_ip;

	if ((err = netconn_gethostbyname(c->p->rhost, &rhost_ip)) != ERR_OK) {
		fprintf(stderr, "tcpfw_acceptor: netconn_gethostbyname (%s) error %d.\n", c->p->rhost, err);
		netconn_close(c->remote);
		return (-1);
	}

	if (netconn_connect(c->remote, &rhost_ip, c->p->rport) != ERR_OK) {
		perror("netconn_connect");
		netconn_close(c->remote);
		return (-1);
	}

	LWIP_DEBUGF(TCPFW_DEBUG, ("tcpfw_acceptor: connected to %s (0x%x).\n", c->p->rhost, ntohl(rhost_ip.addr)));

	sys_thread_new(c->l2r, tcpfw_l2r, c, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
	sys_thread_new(c->r2l, tcpfw_r2l, c, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
}

void
tcpfw_init(in_port_t lport, const char *rhost, in_port_t rport)
{
	i_tcpfw_init(lport, rhost, rport, tcpfw_acceptor);
}

static void
tcpsocks_converse(void *arg)
{
	tcpfwc_t *c = (tcpfwc_t *)arg;
	err_t err;
	struct netbuf *nbuf;
	unsigned char buf[256];
	int n;
	int rhostlen;
	char rhost[MAXHOSTNAMELEN];
	in_port_t rport, lport;
	ip_addr_t rhost_ip, lhost_ip;
	struct sockaddr_in sin;

	/* Wait for the client to tell us their preferred version,
	 * authentication methods, etc. */
	if ((n = read(c->local, buf, 256)) < 1)
		goto kill;

	/* Check SOCKS version. */
	if (buf[0] != 5) {
		LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: unknown SOCKS version: %d.\n", buf[0]));
		goto kill;
	}

	/* Don't really care about which auth you support - we only do 'no auth'. */
	buf[0] = 5; /* SOCKS version. */
	buf[1] = 0; /* no auth. */
	write(c->local, buf, 2);

	/* Get the command. */
	if ((n = read(c->local, buf, 256)) < 1)
		goto kill;

	if (n < 4) {
		LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: short read looking for command.\n"));
		goto kill;
	}

	if ((buf[0] != 5) || /* SOCKS version 5 */
	    (buf[1] != 1) || /* TCP CONNECT command */
	    (buf[2] != 0)) { /* FLAG 0 */
		LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: illegal connect command.\n"));
		goto kill;
	}

	rhostlen = buf[4]; /* Length of hostname. */

	if (buf[3] == 3) { /* Address type: ASCII domain name */
		if (n < (4 + rhostlen + 2)) {
			LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: short read looking for hostname/port.\n"));
			goto kill;
		}

		memset(rhost, 0, MAXHOSTNAMELEN);
		memcpy(rhost, &buf[5], rhostlen);
		rport = (buf[5 + rhostlen] << 8) | (buf[5 + rhostlen + 1]);

		LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: connect to %s:%d.\n", rhost, rport));

		if ((err = netconn_gethostbyname(rhost, &rhost_ip)) != ERR_OK) {
			fprintf(stderr, "tcpsocks_converse: netconn_gethostbyname (%s) error %d.\n", rhost, err);
			goto kill;
		}
	} else if (buf[3] == 1) { /* Address type: "raw" IPv4 address */
		IP4_ADDR(&rhost_ip, buf[4], buf[5], buf[6], buf[7]);
		rport = (buf[8] << 8) | buf[9];
	} else {
		LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: unsupported address type %d.\n", buf[3]));
		goto kill;
	}

	if (netconn_connect(c->remote, &rhost_ip, rport) != ERR_OK) {
		perror("netconn_connect");
		goto kill;
	}

	LWIP_DEBUGF(TCPFW_DEBUG, ("tcpsocks_converse: connected to %s (0x%x)\n", rhost, ntohl(rhost_ip.addr)));

	/* Good to go. */
	buf[0] = 5; /* SOCKS version */
	buf[1] = 0; /* report success */
	buf[2] = 0; /* Reserved */
	buf[3] = 1; /* IPV4 Address and port follow */

	if ((err = netconn_getaddr(c->remote, &lhost_ip, &lport, 1)) != ERR_OK)
		fprintf(stderr, "tcpsocks_converse: netconn_getaddr (%s) error %d.\n", rhost, err);

	buf[4] = lhost_ip.addr >> 24;
	buf[5] = lhost_ip.addr >> 16;
	buf[6] = lhost_ip.addr >> 8;
	buf[7] = lhost_ip.addr;
	buf[8] = lport >> 8;
	buf[9] = lport;

	write(c->local, buf, 10);

	sys_thread_new(c->l2r, tcpfw_l2r, c, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
	sys_thread_new(c->r2l, tcpfw_r2l, c, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);

	return;
 kill:
	close(c->local);
	free(c);
}

static int
tcpsocks_acceptor(tcpfwc_t *c)
{
	sys_thread_new(c->l2r, tcpsocks_converse, c,
		       DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
}

void
tcpsocks_init(in_port_t lport)
{
	i_tcpfw_init(lport, NULL, 0, tcpsocks_acceptor);
}
