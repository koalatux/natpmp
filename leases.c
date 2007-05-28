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
#include <string.h>

#include "die.h"
#include "leases.h"

/* list of leases */
lease * leases;
/* number of allocated leases */
int lease_a;
/* number of leases */
int lease_c;

void allocate_leases(int amount) {
	if (amount > lease_a) {
		lease_a += ALLOCATE_AMOUNT;
	}
	else if (lease_a >= amount + 2 * ALLOCATE_AMOUNT) {
		lease_a -= ALLOCATE_AMOUNT;
	}
	else {
		return;
	}
	leases = realloc(leases, lease_a * sizeof(lease));
	if (leases == NULL) p_die("realloc");
}

int add_lease(lease * a) {
	allocate_leases(lease_c + 1);
	memcpy(&leases[lease_c], a, sizeof(lease));
	return lease_c++;
}

void remove_lease(int i) {
	lease_c--;
	memmove(&leases[i], &leases[i+1], (lease_c-i) * sizeof(lease));
	allocate_leases(lease_c);
}
