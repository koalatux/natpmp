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


#include <stdint.h>


#define ALLOCATE_AMOUNT 8

typedef struct {
	uint32_t expires;
	/* The variables below are stored in network byte order */
	uint32_t client;
	uint16_t private_port;
	uint16_t mapped_port;
	/* protocols: 0x1 tcp, 0x2 udp; 0x0 means no mapping assigned but this port is somehow reserved for that client */
	char protocols;
} lease;


void allocate_leases(const int amount);
int add_lease(const lease * a);
void remove_lease(const int i);
int get_index_by_pointer(const lease * a);
void remove_lease_by_pointer(const lease * a);
lease * get_lease_by_port(const uint16_t port);
lease * get_lease_by_client_port(const uint32_t client, const uint16_t port);
lease * get_next_lease_by_client(const uint32_t client, const lease * prev);
lease * get_next_expired_lease(const uint32_t now, const lease * prev);
