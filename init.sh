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

BIND_ADDRS=""
for IF in $PRIVATE_IFS; do
	ip route | grep "^224\.0\.0\.0/4 dev $IF" > /dev/null || ip route add 224.0.0.0/4 dev $IF
	ADDR=`ip addr show dev $IF | grep "^\s*inet .* $IF\$"| cut -d " " -f 6 | cut -d / -f 1`
	[ -n "$ADDR" ] && BIND_ADDRS+=" --bind $ADDR"
done

./natpmp
#natpmp --public-if $PUBLIC_IF $BIND_ADDRS
