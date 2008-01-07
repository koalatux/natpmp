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


struct lease {
	/* IP addresses and port numbers are stored in network byte order! */
	/* this is a hack for convience, only the fields 1 and 2 are used
	 * in expires */
	union {
		uint32_t client;
		/* protocols are stored with two different expires fields 1 for
		 * udp and 2 for tcp, an expires value of UINT32_MAX indicates
		 * an unused protocol */
		uint32_t expires[3];
	};
	uint16_t private_port;
	uint16_t public_port;
	struct lease *prev;
	struct lease *next;
};


/* time the next leases expire */
extern uint32_t next_lease_expires;
extern int update_expires;


struct lease *add_lease(const struct lease *a);
void remove_lease(struct lease *a);
struct lease *get_lease_by_port(const uint16_t port);
struct lease *get_lease_by_client_port(const uint32_t client,
		const uint16_t port);
struct lease *get_next_lease_by_client(const uint32_t client,
		struct lease *prev);
struct lease *get_next_expired_lease(const uint32_t now,
		struct lease *prev);
void do_update_expires();

#ifdef DEBUG_LEASES
void print_lease(const struct lease *a);
void print_leases();
#endif
