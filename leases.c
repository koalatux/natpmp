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


#include <stdlib.h>

#ifdef DEBUG_LEASES
#include <arpa/inet.h>
#include <stdio.h>
#endif

#include "die.h"
#include "leases.h"

/* linked list of leases */
struct lease *first = NULL;
struct lease *last = NULL;

/* time the next leases expire */
uint32_t next_lease_expires = UINT32_MAX;
/* indicates if the next_lease_expires has to be updated, e.g. on change or
 * removal of a lease */
int update_expires = 0; /* TODO ??? */

/* function that adds a lease to the list of leases */
struct lease *add_lease(const struct lease *a)
{
	struct lease *new = malloc(sizeof(struct lease));
	if (new == NULL) p_die("malloc");
	*new = *a;

	if (last) last->next = new;
	else first = new;
	new->prev = last;
	new->next = NULL;
	last = new;
	return new;
}

/* function that removes a lease from the list of leases */
void remove_lease(struct lease *a)
{
	if (a->prev == NULL && a->next == NULL) {
		first = NULL;
		last = NULL;
	}
	else {
		if (a->prev) a->prev->next = a->next;
		else first = a->next;
		if (a->next) a->next->prev = a->prev;
		else last = a->prev;
	}
	free(a);
}

/* function that returns a lease pointer by public port number, NULL if public
 * port number is still unmapped */
struct lease *get_lease_by_port(const uint16_t port)
{
	if (first == NULL) return NULL;
	struct lease *a = first;
	do {
		if (a->public_port == port) return a;
	} while ((a = a->next));
	return NULL;
}

/* function that returns a lease pointer by client ip address and private port
 * numnber, NULL if no lease found */
struct lease *get_lease_by_client_port(const uint32_t client,
		const uint16_t port)
{
	if (first == NULL) return NULL;
	struct lease *a = first;
	do {
		if (a->client == client && a->private_port == port) return a;
	} while ((a = a->next));
	return NULL;
}

/* function that returns a pointer to the next lease by client ip address, NULL
 * if no leases found, prev is the pointer to the lease from where to search
 * from, NULL to search from beginning */
struct lease *get_next_lease_by_client(const uint32_t client,
		struct lease *prev)
{
	if (first == NULL || (prev && prev->next == NULL)) return NULL;
	struct lease *a = (prev) ? prev->next : first;
	do {
		if (a->client == client) return a;
	} while ((a = a->next));
	return NULL;
}

/* function that returns a pointer to the next expired lease, NULL if no leases
 * found, provide the actual time with now, prev is the pointer to the lease
 * from where to search from, NULL to search from beginning */
struct lease *get_next_expired_lease(const uint32_t now,
		struct lease *prev)
{
	if (first == NULL || (prev && prev->next == NULL)) return NULL;
	struct lease *a = (prev) ? prev->next : first;
	do {
		if (a->expires[1] <= now) return a;
		if (a->expires[2] <= now) return a;
	} while ((a = a->next));
	return NULL;
}

/* function that updates the next_lease_expires variables */
void do_update_expires()
{
	if (!update_expires) return;
	next_lease_expires = UINT32_MAX;
	if (first) {
		struct lease *a = first;
		do {
			if (a->expires[1] < next_lease_expires)
				next_lease_expires = a->expires[1];
			if (a->expires[2] < next_lease_expires)
				next_lease_expires = a->expires[2];
		} while ((a = a->next));
	}
	update_expires = 0;
#ifdef DEBUG_LEASES
	if (debuglevel >= 2)
		printf("next lease expires: %u\n", next_lease_expires);
#endif
}

#ifdef DEBUG_LEASES
/* function that prints a lease */
void print_lease(const struct lease *a)
{
	struct in_addr client = { a->client };
	printf("Client: %s, UDP-Expires: %u, TCP-Expires: %u, Private: %hu, "
			"Public: %hu\n",
			inet_ntoa(client),
			a->expires[1],
			a->expires[2],
			ntohs(a->private_port),
			ntohs(a->public_port));
}

/* function to print all leases */
void print_leases()
{
	printf("----LEASES----\n");
	if (first) {
		struct lease *a = first;
		do {
			print_lease(a);
		} while ((a = a->next));
	}
	printf("--------------\n");
	if (update_expires == 0)
		printf("next lease expires: %u\n", next_lease_expires);
}
#endif
