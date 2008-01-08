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


TARGET_1=192.168.1.16
TARGET_2=192.168.1.211
SOURCE_1=192.168.1.211
SOURCE_2=192.168.1.16
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

# TODO: find a better solution than just waiting one second for answer
answer=$(echo -ne "$(echo -n "$request_packet" | sed -e 's/../\\x&/g')" | nc -unw1 -s $SOURCE_ADDRESS $TARGET_ADDRESS 5351 | od -t x1 | cut -c 9- | sed -e ':a' -e '$bb' -e 'N' -e 'ba' -e ':b' -e 's/[\n ]//g')

size=$(($(echo -n "$answer" | wc -c) / 2))
[ $size -eq 0 ] && fatal_0 "No answer received from $TARGET_ADDRESS."
if [ $size -lt $expected_size ] ; then
	error_1 "Answer packet has wrong size."
	return 1
elif [ $size -gt $expected_size ] ; then
	warn_1 "Answer packet has trailed chunks."
fi

version=$((0x$(echo -n "$answer" | cut -c 1-2)))
[ $version -ne 0 ] && error_1 "Answer packet has wrong version."

opcode=$((0x$(echo -n "$answer" | cut -c 3-4)))
[ $opcode -ne $expected_opcode ] && error_1 "Answer packet has wrong op code."

resultcode=$((0x$(echo -n "$answer" | cut -c 5-8)))
[ $resultcode -ne $expected_resultcode ] && error_1 "Answer packet has wrong result code."

epoch=$((0x$(echo -n "$answer" | cut -c 9-16)))
info_1 "Seconds since start of epoch: $epoch"

return 0
}

function request_mapping () {
r_protocol=$1
r_private_port=$2
r_public_port=$3
r_lifetime=$4

request "$(printf "00%02x0000%04x%04x%08x" $r_protocol $r_private_port $r_public_port $r_lifetime)" 16 $((128 + $r_protocol)) 0 \
|| return 1

private_port=$((0x$(echo -n "$answer" | cut -c 17-20)))
[ $private_port != $r_private_port ] && error_1 "Ansewr packet has wrong private port."

public_port=$((0x$(echo -n "$answer" | cut -c 21-24)))
info_1 "Assigned public port: $public_port"

lifetime=$((0x$(echo -n "$answer" | cut -c 25-32)))
info_1 "Mapping lifetime: $lifetime"
[ $lifetime -gt $r_lifetime ] && error_1 "Lifetime has been raised."
#[ $lifetime -ne $r_lifetime -a $lifetime -lt 3600 ] && warn_1 "Lifetime lower than recommended value."

return 0
}

function set_system() {
if [ "$1" -eq 1 ]; then
	TARGET_ADDRESS=$TARGET_1
	SOURCE_ADDRESS=$SOURCE_1
else
	TARGET_ADDRESS=$TARGET_2
	SOURCE_ADDRESS=$SOURCE_2
fi
}

## start testing ##

# invalid packets tests #
set_system 1

# the opcode is not specified (airport sends 0)
#TODO: don't throw an error on unmatched opcode
info_0 "Trying with unsupported version."
request "1702" 8 $((128 + 0x02)) 1

epoch_start=$epoch
date_start=$(date +%s)

info_0 "Trying with unsupported op code."
request "0017" 8 $((128 + 0x17)) 5

# public ip address test #

info_0 "Trying public IP address request."
if request "0000" 12 128 0; then
	public_ipaddress=$(eval echo "$(echo -n "$answer" | cut -c 17-24 | sed -e 's/../$((0x&))./g;s/.$//')")
	info_1 "Public IP address: $public_ipaddress"
fi

# port range and lifetime limit tests #
# Remember: these limitations are not part of the draft, so only for testing my additions

info_0 "Trying a low port number."
request_mapping 1 80 90 3600 && \
[ $public_port -lt 1024 ] && warn_1 "Port with a low number assigned."

info_0 "Trying a high port number."
request_mapping 2 65535 65500 3600 && \
[ $public_port -ge 65500 ] && warn_1 "Port with a high number assigned."

info_0 "Trying a very short lifetime."
request_mapping 1 2001 2001 1 && \
[ $lifetime -lt 1 ] && warn_1 "Very short lifetime not granted, altough it is foolish."

info_0 "Trying a long lifetime."
request_mapping 1 2000 2000 604800 && \
[ $lifetime -ge 604800 ] && warn_1 "Long lifetime granted."
old_public_port=$public_port

# lease stealing tests #
set_system 2

info_0 "Trying to steal existing mapping."
request_mapping 1 2000 $old_public_port 3600 && \
[ $public_port -eq $old_public_port ] && error_1 "The port was not reserved."

info_0 "Trying to steal companion port of existing mapping."
request_mapping 2 2000 $old_public_port 3600 && \
[ $public_port -eq $old_public_port ] && error_1 "The companion port was not reserved."

# Remember: in the draft nothing is mentioned about assigning a port which belongs to a local process. But assigning the natpmp's port itself should never be granted

info_0 "Trying a port of a listening process on the router."
request_mapping 1 5351 5351 3600 && \
[ $public_port -eq 5351 ] && error_1 "Port of a listening process assigned."

info_0 "Trying a companion port of a listening process on the router."
request_mapping 2 5351 5351 3600 && \
[ $public_port -eq 5351 ] && warn_1 "Companion port of a listening process assigned."

# lease renewing tests #
set_system 1

# Remember: This is a limitation in my implementation; you can only acquire companion ports to the same private port.
info_0 "Trying to get the companion port to a previous mapping."
request_mapping 2 2000 $old_public_port 3600 && \
[ $public_port -ne $old_public_port ] && error_1 "Could not get the companion port."

info_0 "Trying existing mapping with different public port."
request_mapping 2 2000 2200 3600 && \
[ $public_port -ne $old_public_port ] && error_1 "Didn't get the already mapped public port."

# deletion tests #
set_system 2

#info_0 "Trying to delete a foreign mapping."
#request_mapping 1 2000 $old_public_port 0 && \
#[ $lifetime -ne 0 ] && error_1 "Incorrect answer."

info_0 "Trying to delete all UDP mappings."
request_mapping 1 0 0 0 && \
[ $lifetime -ne 0 ] && error_1 "Incorrect answer."

info_0 "Trying to delete all TCP mappings."
request_mapping 2 0 0 0 && \
[ $lifetime -ne 0 ] && error_1 "Incorrect answer."

set_system 1

info_0 "Trying to delete a companion port of a mapping."
request_mapping 2 2000 $old_public_port 0 && \
[ $lifetime -ne 0 ] && error_1 "Incorrect answer."

info_0 "Trying to delete a mapping."
request_mapping 1 2000 $old_public_port 0 && \
[ $lifetime -ne 0 ] && error_1 "Incorrect answer."

info_0 "Trying to delete the mapping again."
request_mapping 1 2000 $old_public_port 0 && \
[ $lifetime -ne 0 ] && error_1 "Incorrect answer."

# lease renewing and expiring tests #

# TODO: renewing to a smaller leasetime
# TODO: renewing to a bigger leasetime
# TODO: simple expiration
# TODO: expiration of only one protocol when only one exists

info_0 "Checking if epoch has been sent correct."
time_diff=$(( ($epoch - $epoch_start) - ($(date +%s) - $date_start) ))
[ $time_diff -gt 1 -o $time_diff -lt -1 ] && error_1 "Epoch is not accurate."

exit $ERROR
