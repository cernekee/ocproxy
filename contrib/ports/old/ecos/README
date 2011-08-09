This part of the lwip-contrib section is the ecos glue  

Check out latest ecos from CVS (>= 19 Jan 2004) so it has the latest io/eth glue for lwip.

You must have the current lwip sources checked out and the scripts here will hopefully generate
a correct EPK suitable for use with ecos.


To make an ecos package:

Run the mkepk script something like this

# EPK=/dir/of/epk LWIP_CVS=/dir/of/checked/out/lwip ./mkepk

This will put the generated EPK in the dir you specify


then add that EPK to your package repository and use it

Build example:
#ecosconfig new edb7xxx kernel
#ecosconfig add lwip
this will default to SLIP connection.If you also
#ecosconfig add net_drivers
you'll be able to configure for ethernet

In both cases set LWIP_MY_ADDR and LWIP_SERV_ADDR (means host/gateway for eth or host/peer for slip)
#ecosconfig tree
#make tests
and you can try any of the five tests included

Tests:
udpecho - echo service port 7 on UDP
tcpecho - ditto on TCP
sockets - as tcpecho but written with the socket API not the lwip specific API
https - http server on port 80 written with the raw API
nc_test_slave - a port of the test with the same name in net/common to lwip. Used to compare 
		lwIP throughput to that of the FreeBSD stack.In this matchup lwIP gets a well deserved 
		silver medal.Not bad for a newcomer ;)
		

Bugreports (or even better patches) at jani@iv.ro not the lwip or ecos mailing lists!!
Only if you peek into the lwip sources and think you found a bug post to lwip-users.
