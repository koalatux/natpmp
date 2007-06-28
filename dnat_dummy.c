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


#include <arpa/inet.h>
#include <stdio.h>

#include "dnat_api.h"

void dnat_init(int argc, char * argv[]) {
	printf("dnat_init(%d, {", argc);
	int i;
	for (i=0; i<argc; i++) {
		printf("\"%s\", ", argv[i]);
	}
	printf("\b\b})\n");
}

int get_dnat_rule_by_mapped_port(const char protocol, const uint16_t mapped_port, uint32_t * client, uint16_t * private_port) {
	printf("get_dnat_rule_by_mapped_port(%hhd, %hu, *, *)\n", protocol, ntohs(mapped_port));
	if (client != NULL) *client = 0;
	if (private_port != NULL) *private_port = 0;
	return 0;
}

int get_dnat_rule_by_client_port(const char protocol, uint16_t * mapped_port, const uint32_t client, const uint16_t private_port) {
	struct in_addr addr = {client};
	printf("get_dnat_rule_by_client_port(%hhd, *, %s, %hu)\n", protocol, inet_ntoa(addr), ntohs(private_port));
	if (mapped_port != NULL) *mapped_port = 0;
	return 0;
}

int create_dnat_rule(const char protocol, const uint16_t mapped_port, const uint32_t client, const uint16_t private_port) {
	struct in_addr addr = {client};
	printf("create_dnat_rule(%hhd, %hu, %s, %hu)\n", protocol, ntohs(mapped_port), inet_ntoa(addr), ntohs(private_port));
	return 0;
}

int destroy_dnat_rule(const char protocol, const uint16_t mapped_port, const uint32_t client, const uint16_t private_port) {
	struct in_addr addr = {client};
	printf("destroy_dnat_rule(%hhd, %hu, %s, %hu)\n", protocol, ntohs(mapped_port), inet_ntoa(addr), ntohs(private_port));
	return 0;
}
