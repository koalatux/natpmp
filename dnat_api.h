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

#ifndef DNAT_API_H
#define DNAT_API_H

#include <stdint.h>

/*
 * IP addresses and port numbers all are in network byte order!
 * create_dnat_rule() and destroy_dnat_rule() must succeed if rule was already
 * there respectively has already gone.
 * Port numbers in a rule with a port range must be considered as existing but
 * must not get destroyed, destroy_dnat_rule() must return 1 if requested to do
 * so. Do the same, if the program recognizes that the given rule was not
 * created by itself, e.g. if the rule is in chain not used by the program. But
 * only do this, if the underlying system provides such information.
 */

/* valid values for protocol */
#define UDP 1
#define TCP 2

/* search DNAT rule for given mapped port, return 1 if found, 0 if not found and -1 on error, set client and private_port if not NULL */
int get_dnat_rule_by_mapped_port(const char protocol, const uint16_t mapped_port, uint32_t * client, uint16_t * private_port);

/* search DNAT rule for given private port and client, return 1 if found, 0 if not found and -1 on error, set mapped_port if not NULL */
int get_dnat_rule_by_client_port(const char protocol, uint16_t * mapped_port, const uint32_t client, const uint16_t private_port);

/* create a DNAT rule with given mapped port, client ip address and private port, return 0 on success and -1 on failure */
int create_dnat_rule(const char protocol, const uint16_t mapped_port, const uint32_t client, const uint16_t private_port);

/* destroy a DNAT rule with given mapped port, client ip address and private port, return 0 on success, 1 when forbidden and -1 on failure */
int destroy_dnat_rule(const char protocol, const uint16_t mapped_port, const uint32_t client, const uint16_t private_port);

#endif /* DNAT_API_H */
