ocproxy
=======

ocproxy is a user-level SOCKS and port forwarding proxy for
[OpenConnect](http://www.infradead.org/openconnect/)
based on lwIP.  When using ocproxy, OpenConnect only handles network
activity that the user specifically asks to proxy, so the VPN interface
no longer "hijacks" all network traffic on the host.

Basic usage
-----------

Commonly used options include:

      -D port                   Set up a SOCKS5 server on PORT
      -L lport:rhost:rport      Connections to localhost:LPORT will be redirected
                                over the VPN to RHOST:RPORT
      -g                        Allow non-local clients.
      -k interval               Send TCP keepalive every INTERVAL seconds, to
                                prevent connection timeouts

ocproxy should not be run directly.  Instead, it should be started by
openconnect using the --script-tun option:

    openconnect --script-tun --script \
        "./ocproxy -L 2222:unix-host:22 -L 3389:win-host:3389 -D 11080" \
        vpn.example.com

Once ocproxy is running, connections can be established over the VPN link
by connecting directly to a forwarded port or by utilizing the builtin
SOCKS server:

    ssh -p2222 localhost
    rdesktop localhost
    socksify ssh unix-host
    tsocks ssh 172.16.1.2
    ...

OpenConnect can (and should) be run as a non-root user when using ocproxy.


Using the SOCKS5 proxy
----------------------

tsocks, Dante, or similar wrappers can be used with non-SOCKS-aware
applications.

Sample tsocks.conf (no DNS):

    server = 127.0.0.1
    server_type = 5
    server_port = 11080

Sample socks.conf for Dante (DNS lookups via SOCKS5 "DOMAIN" addresses):

    resolveprotocol: fake
    route {
            from: 0.0.0.0/0 to: 0.0.0.0/0 via: 127.0.0.1 port = 11080
            command: connect
            proxyprotocol: socks_v5
    }

[FoxyProxy](http://getfoxyproxy.org/) can be used to tunnel Firefox or Chrome
browsing through the SOCKS5 server.  This will send DNS queries through the
VPN connection, and unqualified internal hostnames (e.g. http://intranet/)
should work.  FoxyProxy also allows the user to route requests based on URL
patterns, so that (for instance) certain domains always use the proxy server
but all other traffic connects directly.

It is possible to start several different instances of Firefox, each with
its own separate profile (and hence, proxy settings):

    # initial setup
    firefox -no-remote -ProfileManager

    # run with previous configured profile "vpn"
    firefox -no-remote -P vpn


Building ocproxy
----------------

Dependencies:

 * libevent &gt;= 2.0: *.so library and headers
 * autoconf
 * automake
 * gcc, binutils, make, etc.

Building from git:

    ./autogen.sh
    ./configure
    make


Other possible uses for ocproxy
-------------------------------

 * Routing traffic from different applications/browsers through different VPNs
(or no VPN)
 * Connecting to multiple VPNs or sites concurrently, even if their IP ranges
overlap or their DNS settings are incompatible
 * Situations in which root access is unavailable or undesirable; multiuser
systems

It is possible to write a proxy autoconfig (PAC) script that decides whether
each request should use ocproxy or a direct connection, based on the domain
or other criteria.

ocproxy also works with OpenVPN; the necessary patches are posted
[here](http://thread.gmane.org/gmane.network.openvpn.devel/8478).


Network configuration
---------------------

ocproxy normally reads its network configuration from the following
environment variables set by OpenConnect:

 * `INTERNAL_IP4_ADDRESS`: IPv4 address
 * `INTERNAL_IP4_MTU`: interface MTU
 * `INTERNAL_IP4_DNS`: DNS server list (optional but recommended)
 * `CISCO_DEF_DOMAIN`: default domain name (optional)

The `VPNFD` environment variable tells ocproxy which file descriptor is used
to pass the tunneled traffic.


Credits
-------

Original author: David Edmondson <dme@dme.org>

Current maintainer: Kevin Cernekee <cernekee@gmail.com>

Project home page: <https://github.com/cernekee/ocproxy>
