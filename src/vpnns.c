/*
 * Copyright (c) 2016 Google Inc.
 * Author: Kevin Cernekee <cernekee@gmail.com>
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

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <event2/event.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <net/route.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#define DEFAULT_NAME		"default"
#define DEFAULT_DNS_LIST	"8.8.8.8 8.8.4.4"

struct packet_loop_ctx {
	struct event_base *event_base;
	struct event *tun_event;
	struct event *vpn_event;
	struct event *sig_event;
	int vpn_fd;
	int tun_fd;
};

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	exit(1);
}

static pid_t read_pid(const char *pidfile)
{
	FILE *f = fopen(pidfile, "r");
	int pid = 0;

	if (!f)
		return -1;
	if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
		fclose(f);
		return -1;
	}

	fclose(f);
	return pid;
}

static void write_file(const char *file, const char *data)
{
	FILE *f = fopen(file, "w");

	if (!f)
		die("can't open '%s' for writing\n", file);
	if (fputs(data, f) == EOF || fclose(f) != 0)
		die("error writing to '%s'\n", file);
}

static int enter_ns(pid_t pid, const char *ns_str, int nstype)
{
	char *nsfile;
	int fd;

	if (asprintf(&nsfile, "/proc/%d/ns/%s", pid, ns_str) < 0)
		die("can't allocate memory\n");
	fd = open(nsfile, O_RDONLY);
	free(nsfile);

	if (fd < 0)
		return -1;

	if (setns(fd, nstype) < 0) {
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

static int enter_all(pid_t pid)
{
	if (enter_ns(pid, "user", CLONE_NEWUSER) ||
	    enter_ns(pid, "uts", CLONE_NEWUTS) ||
	    enter_ns(pid, "net", CLONE_NEWNET) ||
	    enter_ns(pid, "mnt", CLONE_NEWNS))
		return -1;
	return 0;
}

static void setup_ipv4(const char *ifname, const char *addr, const char *mask,
		       bool default_route, int mtu)
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	struct rtentry route;

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		die("socket() failed: %s\n", strerror(errno));

	memset(&ifr, 0, sizeof(ifr));
	memset(&route, 0, sizeof(route));

	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_addr.sa_family = AF_INET;

	if (inet_pton(AF_INET, addr, &sin->sin_addr) != 1)
		die("invalid IPv4 address '%s'\n", addr);
	if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) {
		die("can't set IPv4 address on %s: %s\n", ifname,
		    strerror(errno));
	}
	memcpy(&route.rt_gateway, sin, sizeof(*sin));

	if (inet_pton(AF_INET, mask, &sin->sin_addr) != 1)
		die("invalid IPv4 netmask '%s'\n", mask);
	if (ioctl(fd, SIOCSIFNETMASK, &ifr) < 0) {
		die("can't set IPv4 netmask on %s: %s\n", ifname,
		    strerror(errno));
	}

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0)
		die("SIOCGIFFLAGS failed\n");
	ifr.ifr_flags |= IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0)
		die("SIOCGIFFLAGS failed\n");

	if (default_route) {
		sin = (struct sockaddr_in *)&route.rt_dst;
		sin->sin_family = AF_INET;
		sin = (struct sockaddr_in *)&route.rt_genmask;
		sin->sin_family = AF_INET;

		route.rt_flags = RTF_UP | RTF_GATEWAY;

		if (ioctl(fd, SIOCADDRT, &route) < 0)
			die("can't add default route: %s\n", strerror(errno));
	}

	if (mtu) {
		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
		ifr.ifr_mtu = mtu;
		if (ioctl(fd, SIOCSIFMTU, &ifr) < 0)
			die("can't set up MTU: %s\n", strerror(errno));
	}

	close(fd);
}

static void create_ns(const char *pidfile, const char *name)
{
	char str[64];
	uid_t uid = getuid();
	gid_t gid = getgid();

	if (unshare(CLONE_NEWNS | CLONE_NEWNET |
		    CLONE_NEWUTS | CLONE_NEWUSER) < 0)
		die("can't unshare namespaces: %s\n", strerror(errno));

	if (access("/proc/self/setgroups", O_RDONLY) == 0)
		write_file("/proc/self/setgroups", "deny");
	snprintf(str, sizeof(str), "0 %d 1", uid);
	write_file("/proc/self/uid_map", str);
	snprintf(str, sizeof(str), "0 %d 1", gid);
	write_file("/proc/self/gid_map", str);

	snprintf(str, sizeof(str), "%d\n", getpid());
	write_file(pidfile, str);

	if (sethostname(name, strlen(name)) < 0)
		die("can't set hostname: %s\n", strerror(errno));
	setup_ipv4("lo", "127.0.0.1", "255.0.0.0", false, 0);

	char *resolv;
	if (asprintf(&resolv, "%s.dns", pidfile) < 0)
		die("can't allocate memory\n");

	unlink(resolv);
	int fd = open(resolv, O_WRONLY | O_CREAT, 0644);
	if (fd < 0)
		die("can't create local resolv.conf: %s\n", strerror(errno));
	close(fd);

	if (mount(resolv, "/etc/resolv.conf", NULL, MS_BIND, NULL) < 0)
		die("can't mount resolv.conf: %s\n", strerror(errno));
	unlink(resolv);
	free(resolv);
}

static int run(char *file, char **argv)
{
	pid_t pid = fork();
	int rv;

	if (pid < 0)
		die("can't fork: %s\n", strerror(errno));
	else if (pid > 0)
		wait(&rv);
	else {
		if (argv)
			execvp(file, argv);
		else {
			char *arg[2] = { file, NULL };
			execvp(file, arg);
		}
		die("can't exec '%s': %s\n", file, strerror(errno));
	}

	if (!WIFEXITED(rv))
		return 1;
	else
		return WEXITSTATUS(rv);
}

static int do_create(const char *pidfile, const char *name,
		     int argc, char **argv)
{
	pid_t pid = read_pid(pidfile);

	if (pid > 0) {
		if (enter_all(pid) < 0)
			create_ns(pidfile, name);
	} else {
		create_ns(pidfile, name);
	}

	int rv;
	if (argc == 0) {
		char *shell = getenv("SHELL");
		if (shell)
			rv = run(shell, NULL);
		else
			rv = run("/bin/sh", NULL);
	} else {
		rv = run(argv[0], argv);
	}

	/* Unlink if we're the creator */
	if (pid < 0)
		unlink(pidfile);

	return rv == -1 ? 1 : WEXITSTATUS(rv);
}

static void write_pkt(evutil_socket_t in_fd, short what, void *vctx)
{
	struct packet_loop_ctx *ctx = vctx;
	char buffer[2048];
	int out_fd = (in_fd == ctx->tun_fd) ? ctx->vpn_fd : ctx->tun_fd;

	ssize_t len = read(in_fd, buffer, sizeof(buffer));
	if (len < 0) {
		fprintf(stderr, "bad read on fd %d->%d\n", in_fd, out_fd);
		return;
	}

	if (write(out_fd, buffer, len) != len)
		fprintf(stderr, "bad write on fd %d->%d\n", in_fd, out_fd);
}

static void handle_sig(evutil_socket_t in_fd, short what, void *vctx)
{
	struct packet_loop_ctx *ctx = vctx;
	event_base_loopexit(ctx->event_base, NULL);
}

static void do_packet_loop(int vpn_fd, int tun_fd)
{
	struct packet_loop_ctx ctx;

	if (fcntl(vpn_fd, F_SETFL, O_RDWR | O_NONBLOCK) < 0)
		die("can't set O_NONBLOCK on VPN fd: %s\n", strerror(errno));
	if (fcntl(tun_fd, F_SETFL, O_RDWR | O_NONBLOCK) < 0)
		die("can't set O_NONBLOCK on tun fd: %s\n", strerror(errno));

	ctx.vpn_fd = vpn_fd;
	ctx.tun_fd = tun_fd;

	ctx.event_base = event_base_new();
	if (!ctx.event_base)
		die("can't initialize libevent\n");

	ctx.vpn_event = event_new(ctx.event_base, vpn_fd, EV_READ | EV_PERSIST,
				  &write_pkt, &ctx);
	ctx.tun_event = event_new(ctx.event_base, tun_fd, EV_READ | EV_PERSIST,
				  &write_pkt, &ctx);
	ctx.sig_event = event_new(ctx.event_base, SIGHUP, EV_SIGNAL,
				  &handle_sig, &ctx);

	if (!ctx.vpn_event || !ctx.tun_event || !ctx.sig_event)
		die("can't create event structs\n");
	if (event_add(ctx.vpn_event, NULL) ||
	    event_add(ctx.tun_event, NULL) ||
	    event_add(ctx.sig_event, NULL))
		die("can't register event structs\n");

	event_base_dispatch(ctx.event_base);

	event_del(ctx.sig_event);
	event_free(ctx.sig_event);
	event_del(ctx.tun_event);
	event_free(ctx.tun_event);
	event_del(ctx.vpn_event);
	event_free(ctx.vpn_event);
	event_base_free(ctx.event_base);
}

static void setup_ip_from_env(const char *ifname)
{
	char *val;
	int mtu = 0;

	val = getenv("INTERNAL_IP4_MTU");
	if (val)
		mtu = atoi(val);

	val = getenv("INTERNAL_IP4_ADDRESS");
	if (!val)
		die("missing IPv4 address\n");

	setup_ipv4(ifname, val, "255.255.255.255", true, mtu);

	val = getenv("INTERNAL_IP4_DNS");
	if (!val)
		val = DEFAULT_DNS_LIST;

	FILE *f = fopen("/etc/resolv.conf", "w");

	if (!f)
		die("can't open /etc/resolv.conf for writing\n");

	val = strdup(val);

	char *p = val;
	while (1) {
		char *s = strtok(p, " ");
		if (!s)
			break;
		p = NULL;

		if (fprintf(f, "nameserver %s\n", s) < 0)
			die("error writing to resolv.conf\n");
	}
	free(val);

	val = getenv("CISCO_DEF_DOMAIN");
	if (val) {
		if (fprintf(f, "search %s\n", val) < 0)
			die("error writing to resolv.conf\n");
	}

	if (fclose(f) != 0)
		die("error writing to resolv.conf\n");
}

static int do_attach(const char *pidfile, const char *script)
{
	pid_t pid = read_pid(pidfile);

	if (pid < 0)
		die("can't open pidfile '%s'\n", pidfile);
	else {
		if (enter_all(pid) < 0)
			die("can't attach to pid %d\n", pid);
	}

	int tun_fd = open("/dev/net/tun", O_RDWR);
	if (tun_fd < 0)
		die("can't open /dev/net/tun: %s\n", strerror(errno));

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	if (ioctl(tun_fd, TUNSETIFF, (void *)&ifr) < 0)
		die("can't bind tun device: %s\n", strerror(errno));

	if (script) {
		setenv("reason", "connect", 1);
		setenv("TUNDEV", ifr.ifr_name, 1);
		if (system(script)) {
			/* ignore */
		}
	} else {
		setup_ip_from_env(ifr.ifr_name);

	}

	char *vpn_fd_str = getenv("VPNFD");
	if (!vpn_fd_str)
		die("$VPNFD is not set\n");

	int vpn_fd = atoi(vpn_fd_str);
	do_packet_loop(vpn_fd, tun_fd);

	if (script) {
		setenv("reason", "disconnect", 1);
		if (system(script)) {
			/* ignore */
		}
	}

	/* This automatically deletes the tun0 device. */
	close(tun_fd);

	return 0;
}

static void show_version(void)
{
	puts("vpnns - per-app VPN using Linux namespaces");
	puts("Copyright (C) 2016 Google Inc.");
	puts("This program is part of the " PACKAGE_STRING " package.");
	puts("");
	puts("This is free software with ABSOLUTELY NO WARRANTY.");
	puts("For details see the LICENSE file in the source distribution.");
}

static void show_help(void)
{
	puts("Create a new namespace and run COMMAND:");
	puts("  vpnns [ --name <name> ] -- <command> [args...]");
	puts("");
	puts("Attach to an existing namespace and bring up IP networking:");
	puts("  vpnns --attach [ --script <path> ]");
	puts("");
	puts("Options:");
	puts("");
	puts("  --name <id>       Namespace identifier (default: 'default')");
	puts("  --script <path>   Use vpnc-script PATH to set up IP configuration");
	puts("");
}

static struct option longopts[] = {
	{ "attach",		0,	NULL,	'a' },
	{ "name",		1,	NULL,	'n' },
	{ "script",		1,	NULL,	's' },
	{ "help",		0,	NULL,	'h' },
	{ "version",		0,	NULL,	'v' },
	{ NULL }
};

int main(int argc, char **argv)
{
	int rv;
	char *name = DEFAULT_NAME, *pidfile;
	char *script = NULL;
	bool attach = false;

	while (1) {
		rv = getopt_long(argc, argv, "an:s:hv", longopts, NULL);
		if (rv == -1)
			break;

		switch (rv) {
		case 'a':
			attach = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 's':
			script = optarg;
			break;
		case 'h':
		case '?':
			show_help();
			return 1;
		case 'v':
			show_version();
			return 0;
		}
	}

	if (asprintf(&pidfile, "%s/.vpnns-%s", getenv("HOME"), name) < 0)
		die("can't allocate memory\n");

	if (attach)
		return do_attach(pidfile, script);
	else
		return do_create(pidfile, name, argc - optind, &argv[optind]);
}
