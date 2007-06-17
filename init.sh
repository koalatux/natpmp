#!/bin/sh
PUBLIC_IF=eth0
PRIVATE_IFS=eth1_rename

BIND_ADDRS=""
for IF in $PRIVATE_IFS; do
	ip route | grep "^224\.0\.0\.0/4 dev $IF" > /dev/null || ip route add 224.0.0.0/4 dev $IF
	ADDR=`ip addr show dev $IF | grep "^\s*inet .* $IF\$"| cut -d " " -f 6 | cut -d / -f 1`
	[ -n "$ADDR" ] && BIND_ADDRS+=" --bind $ADDR"
done

./natpmp
#natpmp --public-if $PUBLIC_IF $BIND_ADDRS
