#!/bin/bash
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


TARGET_1=192.168.2.1
TARGET_2=192.168.3.1
SOURCE_1=192.168.2.16
SOURCE_2=192.168.3.16
ERROR=0

function fatal_0 () {
echo -e "F: $1"
exit 1
}

function error_1 () {
echo -e "E:\t$1"
ERROR=1
}

function warn_1 () {
echo -e "W:\t$1"
}

function info_0 () {
echo -e "I: $1"
}

function info_1 () {
echo -e "I:\t$1"
}

function request () {
request_packet=$1
expected_size=$2
expected_opcode=$3
expected_resultcode=$4

answer=$(echo -ne "$(echo -n "$request_packet" | sed -e 's/../\\x&/g')" | nc -unq1 -s $SOURCE_ADDRESS $TARGET_ADDRESS 5351 | od -t x1 | cut -c 9- | sed -e ':a;$bb;N;ba;:b;s/[\n ]//g')

size=$(($(echo -n "$answer" | wc -c) / 2))
[ $size -eq 0 ] && fatal_0 "No answer received from $TESTHOST."
if [ $size -ne $expected_size ] ; then
	error_1 "Answer packet has wrong size."
	return
	#TODO really jump to next test.
fi

version=$((0x$(echo -n "$answer" | cut -c 1-2)))
[ $version -ne 0 ] && error_1 "Answer packet has wrong version."

opcode=$((0x$(echo -n "$answer" | cut -c 3-4)))
[ $opcode -ne $expected_opcode ] && error_1 "Answer packet has wrong op code."

resultcode=$((0x$(echo -n "$answer" | cut -c 5-8)))
[ $resultcode -ne $expected_resultcode ] && error_1 "Answer packet has wrong result code."

epoch=$((0x$(echo -n "$answer" | cut -c 9-16)))
info_1 "Seconds since start of epoch: $epoch"
}

function request_mapping () {
r_protocol=$1
r_private_port=$2
r_public_port=$3
r_lifetime=$4

request "$(printf "00%02x0000%04x%04x%08x" $r_protocol $r_private_port $r_public_port $r_lifetime)" 16 $((128 + $r_protocol)) 0

private_port=$((0x$(echo -n "$answer" | cut -c 17-20)))
[ $private_port != $r_private_port ] && error_1 "Ansewr packet has wrong private port."

public_port=$((0x$(echo -n "$answer" | cut -c 21-24)))
info_1 "Assigned public port: $public_port"

lifetime=$((0x$(echo -n "$answer" | cut -c 25-32)))
info_1 "Mapping lifetime: $lifetime"
}

## start testing ##

TARGET_ADDRESS=$TARGET_1
SOURCE_ADDRESS=$SOURCE_1

# invalid packets tests #

info_0 "Trying with unsupported version."
request "1702" 8 $((128 + 0x02)) 1

info_0 "Trying with unsupported op code."
request "0017" 8 $((128 + 0x17)) 5

# public ip address test #

info_0 "Trying public IP address request."
request "0000" 12 128 0
public_ipaddress=$(eval echo "$(echo -n "$answer" | cut -c 17-32 | sed -e 's/../$((0x&))./g;s/.$//')")
info_1 "Public IP address: $public_ipaddress"

# port range and lifetime limit tests #
# Remember: these limitations are not part of the draft, so only for testing my additions

info_0 "Trying port number under allowed range."
request_mapping 1 80 90 3600
[ $public_port -lt 1024 ] && warn_1 "Port under allowed range assigned."

info_0 "Trying port number over allowed range."
request_mapping 2 60600 60660 3600
[ $public_port -gt 60000 ] && warn_1 "Port over allowed range assigned."

info_0 "Trying too long lifetime."
request_mapping 1 2000 2000 36000
[ $lifetime -gt 7200 ] && warn_1 "Too long lifetime allowed."

old_public_port=$public_port

# lease stealing tests #

TARGET_ADDRESS=$TARGET_2
SOURCE_ADDRESS=$SOURCE_2

info_0 "Trying to steal existing mapping."
request_mapping 1 2600 $old_public_port 3600
[ $public_port -eq $old_public_port ] && error_1 "The port was not reserved."

info_0 "Trying to steal companion port of existing mapping."
request_mapping 2 2700 $old_public_port 3600
[ $public_port -eq $old_public_port ] && error_1 "The companion port was not reserved."

TARGET_ADDRESS=$TARGET_1
SOURCE_ADDRESS=$SOURCE_1

# lease renewing tests #

# Remember: This is a limitation in my implementation; you can only acquire companion ports to the same private port.
info_0 "Trying to get the companion port to a previous mapping."
request_mapping 2 2000 $old_public_port 3600
[ $public_port -ne $old_public_port ] && error_1 "Could not get the companion port."

info_0 "Trying existing mapping with different public port."
request_mapping 2 2000 2200 3600
[ $public_port -ne $old_public_port ] && error_1 "Didn't get the already mapped public port."

# lease renewing and expiring tests #

# TODO: renewing to a smaller leasetime
# TODO: renewing to a bigger leasetime
# TODO: simple expiration
# TODO: expiration of only one protocol when only one exists

# lease deletion tests #

# TODO: single deletion
# TODO: repeated deletion
# TODO: all deletion

exit $ERROR
