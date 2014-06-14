AUTOMAKE_OPTIONS	= foreign subdir-objects

AM_CFLAGS		= -Dunix -DIPv4 -DLWIP_DEBUG \
			  -I$(srcdir)/lwip/src \
			  -I$(srcdir)/lwip/src/include \
			  -I$(srcdir)/lwip/src/include/ipv4 \
			  -I$(srcdir)/contrib/ports/unix/include \
			  -I$(srcdir)/contrib/ports/unix/proj/ocproxy \
			  -DDEFAULT_THREAD_STACKSIZE=65536 -DTCPIP_THREAD_STACKSIZE=65536

LWIP_SOURCES		:= \
			   lwip/src/include/lwip/api.h \
			   lwip/src/include/lwip/api_msg.h \
			   lwip/src/include/lwip/arch.h \
			   lwip/src/include/lwip/debug.h \
			   lwip/src/include/lwip/def.h \
			   lwip/src/include/lwip/dhcp.h \
			   lwip/src/include/lwip/dns.h \
			   lwip/src/include/lwip/err.h \
			   lwip/src/include/lwip/init.h \
			   lwip/src/include/lwip/mem.h \
			   lwip/src/include/lwip/memp.h \
			   lwip/src/include/lwip/memp_std.h \
			   lwip/src/include/lwip/netbuf.h \
			   lwip/src/include/lwip/netdb.h \
			   lwip/src/include/lwip/netifapi.h \
			   lwip/src/include/lwip/netif.h \
			   lwip/src/include/lwip/opt.h \
			   lwip/src/include/lwip/pbuf.h \
			   lwip/src/include/lwip/raw.h \
			   lwip/src/include/lwip/sio.h \
			   lwip/src/include/lwip/snmp_asn1.h \
			   lwip/src/include/lwip/snmp.h \
			   lwip/src/include/lwip/snmp_msg.h \
			   lwip/src/include/lwip/snmp_structs.h \
			   lwip/src/include/lwip/sockets.h \
			   lwip/src/include/lwip/stats.h \
			   lwip/src/include/lwip/sys.h \
			   lwip/src/include/lwip/tcp.h \
			   lwip/src/include/lwip/tcp_impl.h \
			   lwip/src/include/lwip/tcpip.h \
			   lwip/src/include/lwip/timers.h \
			   lwip/src/include/lwip/udp.h \
			   lwip/src/include/netif/etharp.h \
			   lwip/src/include/netif/ppp_oe.h \
			   lwip/src/include/netif/slipif.h \
			   lwip/src/core/def.c \
			   lwip/src/core/dns.c \
			   lwip/src/core/init.c \
			   lwip/src/core/mem.c \
			   lwip/src/core/memp.c \
			   lwip/src/core/netif.c \
			   lwip/src/core/pbuf.c \
			   lwip/src/core/stats.c \
			   lwip/src/core/sys.c \
			   lwip/src/core/tcp.c \
			   lwip/src/core/tcp_in.c \
			   lwip/src/core/tcp_out.c \
			   lwip/src/core/timers.c \
			   lwip/src/core/udp.c \
			   lwip/src/include/ipv4/lwip/autoip.h \
			   lwip/src/include/ipv4/lwip/icmp.h \
			   lwip/src/include/ipv4/lwip/igmp.h \
			   lwip/src/include/ipv4/lwip/inet_chksum.h \
			   lwip/src/include/ipv4/lwip/inet.h \
			   lwip/src/include/ipv4/lwip/ip_addr.h \
			   lwip/src/include/ipv4/lwip/ip_frag.h \
			   lwip/src/include/ipv4/lwip/ip.h \
			   lwip/src/core/ipv4/icmp.c \
			   lwip/src/core/ipv4/inet.c \
			   lwip/src/core/ipv4/inet_chksum.c \
			   lwip/src/core/ipv4/ip.c \
			   lwip/src/core/ipv4/ip_addr.c \
			   lwip/src/core/ipv4/ip_frag.c \
			   lwip/src/api/api_lib.c \
			   lwip/src/api/api_msg.c \
			   lwip/src/api/err.c \
			   lwip/src/api/netdb.c \
			   lwip/src/api/tcpip.c \
			   contrib/ports/unix/include/arch/cc.h \
			   contrib/ports/unix/include/arch/perf.h \
			   contrib/ports/unix/include/arch/sys_arch.h \
			   contrib/ports/unix/include/netif/delif.h \
			   contrib/ports/unix/include/netif/dropif.h \
			   contrib/ports/unix/include/netif/fifo.h \
			   contrib/ports/unix/include/netif/list.h \
			   contrib/ports/unix/include/netif/pcapif.h \
			   contrib/ports/unix/include/netif/sio.h \
			   contrib/ports/unix/include/netif/tapif.h \
			   contrib/ports/unix/include/netif/tcpdump.h \
			   contrib/ports/unix/include/netif/tunif.h \
			   contrib/ports/unix/include/netif/unixif.h \
			   contrib/ports/unix/lwip_chksum.c \
			   contrib/ports/unix/perf.c \
			   contrib/ports/unix/sys_arch.c \
			   contrib/ports/unix/netif/list.c \
			   contrib/ports/unix/netif/tcpdump.c

APP_SOURCES		:= \
			   contrib/ports/unix/proj/ocproxy/ocproxy.c \
			   contrib/ports/unix/proj/ocproxy/lwipopts.h

bin_PROGRAMS		= ocproxy
ocproxy_SOURCES		= $(LWIP_SOURCES) $(APP_SOURCES)

dist_man_MANS		= contrib/ports/unix/proj/ocproxy/ocproxy.1
dist_doc_DATA		= README
dist_noinst_SCRIPTS	= autogen.sh

EXTRA_DIST		= .gitignore
DISTCLEANFILES		= *~
