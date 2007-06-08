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


/*
 * ip addresses and port numbers all are in network byte order!
 * create_dnat_rule() and destroy_dnat_rule() may return 0 instead of 1 as well, this may be restricted in later versions.
 * port numbers in a rule with a port range must be considered as existing but must not get destroyed. return -1 if requested to do so.
 */

/* search DNAT rule with given mapped port, return client ip address, 0 if not found and -1 on error, set private_port if not NULL */
uint32_t get_dnat_ruledest(uint16_t mapped_port, uint16_t * private_port);

/* create a DNAT rule with given mapped port, client ip address and private port, return 0 on success, 1 when rule already existed and -1 on failure */
int create_dnat_rule(uint16_t mapped_port, uint32_t client, uint16_t private_port);

/* destroy a DNAT rule with given mapped port, client ip address and private port, return 0 on success, 1 when rule not found and -1 on failure */
int destroy_dnat_rule(uint16_t mapped_port, uint32_t client, uint16_t private_port);
