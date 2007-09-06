/*
 *    natpmp - an implementation of NAT-PMP
 *    Copyright (C) 2007  Adrian Friedli
 *    Copyright (C) 2007  Simon Neininger
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


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "dnat_api.h"
#include "die.h"

#define CHAIN_NAME_MAXSIZE 30
#define DEFAULT_CHAIN_NAME "natpmp"

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

char chain_name[32] = DEFAULT_CHAIN_NAME;

void dnat_init(int argc, char * argv[]) {
	if (argc > 1) die("Give only name of iptables chain as backend option.");
	if (argc == 0) {
		printf("Using name of iptables chain default: \"" DEFAULT_CHAIN_NAME "\"\n");
		return;
	}
	if (*argv[0] == '\0') die("Backend option is a very little bit too short.");
	if (strlen(argv[0]) > CHAIN_NAME_MAXSIZE) die("Backend option is too long.");
	strncpy(chain_name, argv[0], sizeof(chain_name));
	printf("Using name of iptables chain: \"%s\"\n", chain_name);
}

static const char * proto(const char protocol) {
	if(protocol == UDP) return "udp";
	if(protocol == TCP) return "tcp";
	die("proto: invalid protocol");
}

/* function that gets wrapped by create_dnat_rule() and destroy_dnat_rule() */
int change_dnat_rule(const char c_arg, const char protocol, const uint16_t public_port, const uint32_t client, const uint16_t private_port) {
	char command[256];
	struct in_addr client_addr;
	client_addr.s_addr = client;

	snprintf(command, sizeof(command),
			"iptables -t nat -%c %s -p %s --dport %d -j DNAT --to-destination %s:%d",
			c_arg, chain_name, proto(protocol),ntohs(public_port), inet_ntoa(client_addr), ntohs(private_port));

	if(system(command)) return -1;
	return 0;
}

/* search DNAT rule for given public port, return 1 if found, 0 if not found and -1 on error, set client and private_port if not NULL */
int get_dnat_rule_by_public_port(const char protocol __attribute__ ((unused)), const uint16_t public_port __attribute__ ((unused)), uint32_t * client __attribute__ ((unused)), uint16_t * private_port __attribute__ ((unused))) {
	return 0;
}

/* search DNAT rule for given private port and client, return 1 if found, 0 if not found and -1 on error, set public_port if not NULL */
int get_dnat_rule_by_client_port(const char protocol __attribute__ ((unused)), uint16_t * public_port __attribute__ ((unused)), const uint32_t client __attribute__ ((unused)), const uint16_t private_port __attribute__ ((unused))) {
	return 0;
}

/* create a DNAT rule with given public port, client ip address and private port, return 0 on success and -1 on failure */
int create_dnat_rule(const char protocol, const uint16_t public_port, const uint32_t client, const uint16_t private_port) {
	return change_dnat_rule('A', protocol, public_port, client, private_port);
}

/* destroy a DNAT rule with given public port, client ip address and private port, return 0 on success, 1 when forbidden and -1 on failure */
int destroy_dnat_rule(const char protocol, const uint16_t public_port, const uint32_t client, const uint16_t private_port) {
	return change_dnat_rule('D', protocol, public_port, client, private_port);
}
