/*
 *    natpmp - an implementation of NAT-PMP
 *    Copyright (C) 2007  Adrian Friedli
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#define NATPMP_PORT 5351

#define NATPMP_VERSION 0
#define NATPMP_ANSFLAG 0x80
#define NATPMP_PUBLICIPADDRESS 0
#define NATPMP_MAP_UDP 1
#define NATPMP_MAP_TCP 2
#define NATPMP_SUCCESS 0
#define NATPMP_UNSUPPORTEDVERSION 1
#define NATPMP_REFUSED 2
#define NATPMP_NETFAILURE 3
#define NATPMP_OUTOFRESOURCES 4
#define NATPMP_UNSUPPORTEDOP 5


typedef struct {
	uint8_t version;
	uint8_t op;
} _natpmp_header;

typedef struct{
	uint16_t result;
	uint32_t epoch;
} _natpmp_answer;

typedef struct{
	in_port_t private_port;
	in_port_t public_port;
	uint32_t lifetime;
} _natpmp_mapping;


typedef struct {
	_natpmp_header header;
} natpmp_packet_dummy_request, natpmp_packet_publicipaddress_request;

typedef struct {
	_natpmp_header header;
	_natpmp_answer answer;
} natpmp_packet_dummy_answer;

typedef struct {
	_natpmp_header header;
	_natpmp_answer answer;
	in_addr_t public_ip_address;
} natpmp_packet_publicipaddress_answer;

typedef struct {
	_natpmp_header header;
	uint16_t _reserved;
	_natpmp_mapping mapping;
} natpmp_packet_map_request;

typedef struct {
	_natpmp_header header;
	_natpmp_answer answer;
	_natpmp_mapping mapping;
} natpmp_packet_map_answer;

typedef union {
	natpmp_packet_dummy_request dummy;
	natpmp_packet_publicipaddress_request publicipaddress;
	natpmp_packet_map_request map;
} natpmp_packet_request;

typedef union {
	natpmp_packet_dummy_answer dummy;
	natpmp_packet_publicipaddress_answer publicipaddress;
	natpmp_packet_map_answer map;
} natpmp_packet_answer;
