#!/bin/sh
#
#    natpmp - an implementation of NAT-PMP
#    Copyright (C) 2007  Adrian Friedli
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc.,
#   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#


PUBLIC_IF=eth0
PRIVATE_IFS=eth2
IPTABLES_CHAIN=natpmp

# Flush all the rules in the natpmp chain, or create it, if it doesn't exists.
/sbin/iptables -t nat -F $IPTABLES_CHAIN 2>/dev/null || \
/sbin/iptables -t nat -N $IPTABLES_CHAIN

# Handle all incoming connections in the natpmp chain.
/sbin/iptables -t nat -D PREROUTING -j $IPTABLES_CHAIN 2>/dev/null || true
/sbin/iptables -t nat -A PREROUTING -j $IPTABLES_CHAIN
#-i $PUBLIC_IF

# Iterate through the private interfaces.
BIND_ARGS=""
for IF in $PRIVATE_IFS; do
	# Get the IP address of this interface.
	ADDR=`/sbin/ip addr show dev $IF 2>/dev/null | grep "^\s*inet .* $IF\$" | cut -d " " -f 6 | cut -d / -f 1`
	if [ -n "$ADDR" ] ; then
		# Add the IP address to the argument list.
		BIND_ARGS="$BIND_ARGS --bind \"$ADDR\""
		# Add the multicast route for this interface if it doesn't exist already.
		/sbin/ip route | grep "^224\.0\.0\.0/4 dev $IF" > /dev/null || /sbin/ip route add 224.0.0.0/4 dev $IF
	else
		echo "Could not get IP address of interface $IF. Skipping." >&2
	fi
done

if [ -z "$BIND_ARGS" ] ; then
	echo "No IP addresses to bind to. Exiting." >&2
	exit 1
fi

exec ./natpmp
#exec /usr/sbin/natpmp --pub-if "$PUBLIC_IF" $BIND_ARGS -- --chain "$IPTABLES_CHAIN"
