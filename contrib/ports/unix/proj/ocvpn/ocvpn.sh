#!/bin/sh

#* reason                       -- why this script was called, one of: pre-init connect disconnect
#* VPNGATEWAY                   -- vpn gateway address (always present)
#* TUNDEV                       -- tunnel device (always present)
#* INTERNAL_IP4_ADDRESS         -- address (always present)
#* INTERNAL_IP4_NETMASK         -- netmask (often unset)
#* INTERNAL_IP4_NETMASKLEN      -- netmask length (often unset)
#* INTERNAL_IP4_NETADDR         -- address of network (only present if netmask is set)
#* INTERNAL_IP4_DNS             -- list of dns serverss
#* INTERNAL_IP4_NBNS            -- list of wins servers
#* CISCO_DEF_DOMAIN             -- default domain name
#* CISCO_BANNER                 -- banner from server
#* CISCO_SPLIT_INC              -- number of networks in split-network-list
#* CISCO_SPLIT_INC_%d_ADDR      -- network address
#* CISCO_SPLIT_INC_%d_MASK      -- subnet mask (for example: 255.255.255.0)
#* CISCO_SPLIT_INC_%d_MASKLEN   -- subnet masklen (for example: 24)
#* CISCO_SPLIT_INC_%d_PROTOCOL  -- protocol (often just 0)
#* CISCO_SPLIT_INC_%d_SPORT     -- source port (often just 0)
#* CISCO_SPLIT_INC_%d_DPORT     -- destination port (often just 0)

exec >/tmp/ocfw.log.$$ 2>&1

# Should really use them all.
dns=`echo $INTERNAL_IP4_DNS | awk '{print \$1;}'`

# DMALLOC_OPTIONS=debug=0x4f4ed03,log=/tmp/dmalloc.log
# export DMALLOC_OPTIONS

valgrind --log-file=/tmp/valgrind.log.$$ --leak-check=full --track-origins=yes \
    --malloc-fill=de --free-fill=ad \
./ocvpn \
    -v \
    -i $INTERNAL_IP4_ADDRESS \
    -n $INTERNAL_IP4_NETMASK \
    -g $VPNGATEWAY \
    -d $dns \
    -D 11080 \
    -L 16667:irc-sfbay.us.oracle.com:6667 \
    -L 18123:emea-proxy.uk.oracle.com:80 \
    -L 18124:emeacache.uk.oracle.com:80
