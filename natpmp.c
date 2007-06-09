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
//#include <sys/types.h>
#include <sys/ioctl.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
#include <net/if.h>
#include <poll.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "die.h"
#include "leases.h"
#include "dnat_api.h"
#include "natpmp_defs.h"

#define PUBLIC_IFNAME "eth0"
#define MAX_LIFETIME 7200 /* recommended value for lifetime: 3600 s */
#define PORT_RANGE_LOW 1024 /* ports below 1024 are restricted ports */
#define PORT_RANGE_HIGH 60000 /* 65535 is the highest port available */
#define PORT_LOW_OFFSET 8000 /* gives us fancy port number 8080 for requested port 80 */
/* PORT_RANGE_LOW <= PORT_RANGE_HIGH else bahaviour is undefined
 * PORT_RANGE_HIGH <= 65535 else range not guaranteed
 * PORT_LOW_OFFSET >= PORT_RANGE_LOW else range not guaranteed
 * PORT_LOW_OFFSET + PORT_RANGE_LOW + 1 <= 65535 else range not guaranteed
 * PORT_LOW_OFFSET + PORT_RANGE_LOW + 1 <= PORT_RANGE_HIGH else offset useless, range still guaranteed */

/* time this daemon has been started or tables got refreshed */
uint32_t timestamp;

/* list of leases */
extern lease * leases;
/* number of allocated leases */
extern int lease_a;
/* number of leases */
extern int lease_c;

/* list of socket file descriptors */
struct pollfd * ufd_v;
/* number of sockets */
int ufd_c;

/* function for allocating memory */
void allocate_all() {
	/* allocate memory for leases */
	lease_c = 0;
	lease_a = ALLOCATE_AMOUNT;
	leases = malloc(ALLOCATE_AMOUNT * sizeof(lease));
	if (leases == NULL) p_die("malloc");

	/* allocate memory for sockets */
	ufd_c = 1;
	ufd_v = malloc(ufd_c * sizeof(struct pollfd));
	if (ufd_v == NULL) p_die("malloc");
}

/* close all sockets and free allocated memory */
void close_all() {
	int i;
	for (i=0; i<ufd_c; i++) {
		close(ufd_v[i].fd);
	}
	free(ufd_v);
	free(leases);
}

/* function for sending */
void udp_send_r(const int fd, const struct sockaddr_in * t_addr, const void * data, const size_t len) {
	int err = sendto(fd, data, len, MSG_DONTROUTE, (struct sockaddr *) t_addr, sizeof(struct sockaddr_in));
	if (err == -1) p_die("sendto");
}

/* return seconds since daemon started or tables got refreshed */
uint32_t get_epoch() {
	return htonl(time(NULL) - timestamp);
};

/* function that returns local ip address of an interface */
struct in_addr get_ip_address(const char * ifname) {
	struct ifreq req;
	if (strlen(ifname) >= sizeof(req.ifr_ifrn.ifrn_name) - 1 ) die("Name of interface too long.");
	strcpy(req.ifr_ifrn.ifrn_name, ifname);
	{
		int tfd = socket(PF_INET, SOCK_STREAM, 0);
		int err = ioctl(tfd, SIOCGIFADDR, &req);
		close(tfd);
		if (err == -1) p_die("ioctl(SIOCGIFADDR)");
	}
	struct sockaddr_in * req_addr = (struct sockaddr_in *) &req.ifr_ifru.ifru_addr;
	return (struct in_addr) req_addr->sin_addr;
}

/* send a NAT-PMP packet */
void send_natpmp_packet(const int ufd, const struct sockaddr_in * t_addr, natpmp_packet_answer * packet, size_t size) {
	packet->dummy.header.version = NATPMP_VERSION;
	packet->dummy.header.op |= NATPMP_ANSFLAG;
	packet->dummy.answer.epoch = get_epoch();
	udp_send_r(ufd, t_addr, packet, size);
}

/* create or remove mappings */
void handle_map_request(const int ufd, const struct sockaddr_in * t_addr, const natpmp_packet_map_request * request_packet) {
	char protocol = (char) request_packet->header.op;
	uint32_t client = t_addr->sin_addr.s_addr;

	/* copy some values to the answer packet */
	natpmp_packet_map_answer answer_packet;
	answer_packet.header.op = request_packet->header.op;
	answer_packet.answer.result = NATPMP_SUCCESS;
	answer_packet.mapping.private_port = request_packet->mapping.private_port;
	/* remember: public_port and mapped_port are the same, but differ in name for convience, mapped_port is used where a mapping exists. */
	answer_packet.mapping.public_port = request_packet->mapping.public_port;
	answer_packet.mapping.lifetime = request_packet->mapping.lifetime;

	if (answer_packet.mapping.lifetime != 0) {
		/* creating a mapping is requested */

		if (answer_packet.mapping.lifetime > MAX_LIFETIME) {
			/* lifetime too high, downgrade */
			answer_packet.mapping.lifetime = MAX_LIFETIME;
		}

		lease * a = get_lease_by_client_port(client, answer_packet.mapping.private_port);
		if (a != NULL) {
			/* lease exists, add protocol, update expiration time and answer with mapped port */
			a->protocols |= (char) answer_packet.header.op;
			a->expires = time(NULL) + ntohl(answer_packet.mapping.lifetime);
			answer_packet.mapping.mapped_port = a->mapped_port;
		}
		else {
			/* no lease exists, check for manual mapping */
			uint16_t mapped_port;
			int b = get_dnat_rule_by_client_port(protocol, &mapped_port, client, answer_packet.mapping.private_port);
			if (b == -1) die("get_dnat_rule_by_client_port");
			else if (b == 1) {
				/* manual mapping exists, answer with mapped port */
				answer_packet.mapping.mapped_port = mapped_port;
			}
			else {
				/* no lease and no manual mapping exist, find a valid port and create a lease */

				/* assure the port is in the allowed range */
				if (answer_packet.mapping.public_port < PORT_RANGE_LOW)
					answer_packet.mapping.public_port += PORT_LOW_OFFSET;
				if (answer_packet.mapping.public_port > PORT_RANGE_HIGH)
					answer_packet.mapping.public_port = answer_packet.mapping.public_port %
						(PORT_RANGE_HIGH - PORT_RANGE_LOW + 1) + PORT_RANGE_LOW;

				/* find a free port */
				uint16_t try_count = 0;
				while (1) {
					if (try_count++ > PORT_RANGE_HIGH - PORT_RANGE_LOW) {
						/* all ports checked, no free port found, restore variables and answer with out of resources */
						answer_packet.mapping.public_port = request_packet->mapping.public_port;
						answer_packet.mapping.lifetime = request_packet->mapping.lifetime;
						answer_packet.answer.result = NATPMP_OUTOFRESOURCES;
						break;
					}
					if (get_lease_by_port(answer_packet.mapping.public_port) == NULL &&
							get_dnat_rule_by_mapped_port(TCP, answer_packet.mapping.public_port, NULL, NULL) == 0 &&
							get_dnat_rule_by_mapped_port(UDP, answer_packet.mapping.public_port, NULL, NULL) == 0) {
						/* TODO: acquiring the companion port to a manual mapping can be allowed to the same client */
						/* these parameters are valid for mapping */

						/* add the lease to the database */
						{
							lease c;
							c.expires = time(NULL) + ntohl(answer_packet.mapping.lifetime);
							c.client = client;
							c.private_port = answer_packet.mapping.private_port;
							c.mapped_port = answer_packet.mapping.mapped_port;
							c.protocols = (char) answer_packet.header.op;
							add_lease(&c);
						}

						/* create the mapping*/
						{
							int c = create_dnat_rule(
									answer_packet.header.op,
									answer_packet.mapping.mapped_port,
									client,
									answer_packet.mapping.private_port);
							if (c == -1) die("create_dnat_rule");
						}
						break;
					}

					if (answer_packet.mapping.public_port >= PORT_RANGE_HIGH) {
						answer_packet.mapping.public_port = PORT_RANGE_LOW;
					}
					else {
						answer_packet.mapping.public_port++;
					}
				}
			}
		}
	}
	else {
		/* remove a mapping */
		lease * a;
		int remove_all;

		if (answer_packet.mapping.mapped_port == 0 && answer_packet.mapping.private_port == 0) {
			/* removing all mappings of client (but only for requested protocol) */
			remove_all = 1;
		}
		else {
			/* only removing a single mapping */
			remove_all = 0;
			a = get_lease_by_client_port(client, answer_packet.mapping.private_port);
		}

		while(remove_all == 0 || (a = get_next_lease_by_client(client, NULL)) != NULL) {
			if (a != NULL) {
				/* update used protocols of lease */
				a->protocols &= ~protocol;
				if (a->protocols == 0) {
					/* lease is no more used, remove it */
					remove_lease_by_pointer(a);
				}

				/* destroy mapping */
				int b = destroy_dnat_rule(protocol, a->mapped_port, client, a->private_port);
				if (b == -1) die("destroy_dnat_rule");
				else if (b == 1) {
					/* mapping may not be destroyed, it's a manual mapping, answer with refused */
					answer_packet.answer.result = NATPMP_REFUSED;
				}
			}
			else {
				/* lease not found, check for manual mapping */
				int b = get_dnat_rule_by_client_port(protocol, NULL, client, answer_packet.mapping.private_port);
				if (b == -1) die("get_dnat_rule_by_client_port");
				else if (b == 1) {
					/* manual mapping found, answer with refused */
					answer_packet.answer.result = NATPMP_REFUSED;
				}
			}
		}

		if (answer_packet.answer.result == NATPMP_SUCCESS) {
			/* result code, public port and lifetime set to 0 indicate a successful deletion */
			answer_packet.mapping.public_port = 0;
		}
	}

	/* fire the packet to the client */
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &answer_packet, sizeof(natpmp_packet_map_answer));
}

/* being called on unsupported requests */
void handle_unsupported_request(const int ufd, const struct sockaddr_in * t_addr, const natpmp_packet_dummy_request * request_packet, const uint16_t result) {
	natpmp_packet_dummy_answer answer_packet;
	answer_packet.header.op = request_packet->header.op;
	answer_packet.answer.result = result;
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &answer_packet, sizeof(natpmp_packet_dummy_answer));
}

/* answer with public IP address */
void send_publicipaddress(const int ufd, const struct sockaddr_in * t_addr) {
	/* TODO: answer with Network Failure if no ip address found on the public port */
	natpmp_packet_publicipaddress_answer packet;
	packet.header.op = NATPMP_PUBLICIPADDRESS;
	packet.answer.result = NATPMP_SUCCESS;
	packet.public_ip_address = get_ip_address(PUBLIC_IFNAME).s_addr;
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &packet, sizeof(natpmp_packet_publicipaddress_answer));
}

/* initialize and bind udp */
void udp_init(int * ufd, const char * listen_address, const uint16_t listen_port) {
	/* create UDP socket */
	//*ufd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	*ufd = socket(PF_INET, SOCK_DGRAM, 0);
	if (*ufd == -1) p_die("socket");

	/* store local address and port to bind to */
	struct sockaddr_in s_addr;
	memset(&s_addr, 0, sizeof(struct sockaddr_in));
	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(listen_port);
	{
		int err = inet_aton(listen_address, &s_addr.sin_addr);
		if (err == 0) p_die("inet_aton");
	}

	/* bind here */
	{
		int err = bind(*ufd, (struct sockaddr *) &s_addr, sizeof(struct sockaddr_in));
		if (err == -1) p_die("bind");
	}
}

/* function to fork to background and exit parent */
void fork_to_background() {
	pid_t child = fork();
	if (child == -1) p_die("fork");
	else if (child != 0) {
		fprintf(stderr, "Forked into background.\n");
		printf("%i\n", child);
		exit(EXIT_SUCCESS);
	}
}

int main() {
	/* fork into background, must be called before registering atexit functions */
	//fork_to_background();
	
	/* test create_dnat_rule() XXX */
	{
		struct in_addr address;
		inet_aton("192.168.1.2", &address);
		create_dnat_rule(1, htons(81), address.s_addr, htons(80));
		create_dnat_rule(2, htons(81), address.s_addr, htons(80));
	}

	/* register function being called on exit() */
	{
		int err = atexit(close_all);
		if (err != 0) die("atexit");
	}

	/* allocate some memory and set some variables */
	allocate_all();

	/* set timestamp TODO move to where tables get (re)loaded */
	timestamp = time(NULL);

	/* initialize sockets */
	{
		int i;
		for (i=0; i<ufd_c; i++) {
			udp_init(&ufd_v[i].fd, "0.0.0.0", NATPMP_PORT);

			/* prepare data structures for poll */
			ufd_v[i].events = POLLIN;
		}
	}

	fprintf(stderr, "IP address of %s: %s\n", PUBLIC_IFNAME, inet_ntoa(get_ip_address(PUBLIC_IFNAME)));

	{
		/* socket index */
		int s_i = -1;

		/* main loop */
		while (1) {
			/* construct packet, where the received data ist stored to */
			natpmp_packet_request packet_request;
			memset(&packet_request, 0, sizeof(natpmp_packet_request));

			/* wait until something is received */
			{
				int err = poll(ufd_v, ufd_c, -1);
				if (err == -1) p_die("poll");
			}

			/* there, the information of the sender are stored to */
			struct sockaddr_in t_addr;

			ssize_t pkgsize;
			while (1) {
				if (++s_i >= ufd_c) s_i = 0;

				memset(&t_addr, 0, sizeof(struct sockaddr_in));
				socklen_t t_len = sizeof(struct sockaddr_in);

				pkgsize = recvfrom(ufd_v[s_i].fd, &packet_request, sizeof(natpmp_packet_request), MSG_DONTWAIT,
						(struct sockaddr *) &t_addr, &t_len);
				if (pkgsize == -1) p_die("recvfrom");
				if (pkgsize != EAGAIN && pkgsize != 0) break;
			}

			/* check for wrong or unsupported packets */
			if (pkgsize < (ssize_t) sizeof(natpmp_packet_dummy_request)) continue; /* TODO errorlog */
			if (packet_request.dummy.header.version != NATPMP_VERSION) {
				handle_unsupported_request(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDVERSION);
				continue;
			}
			if (packet_request.dummy.header.op & NATPMP_ANSFLAG) continue;

			/* do things depending on the packet's content */
			switch (packet_request.dummy.header.op) {
				case NATPMP_PUBLICIPADDRESS :
					if (pkgsize != sizeof(natpmp_packet_publicipaddress_request)) continue; /* TODO errorlog */
					send_publicipaddress(ufd_v[s_i].fd, &t_addr);
					break;
				case NATPMP_MAP_UDP :
				case NATPMP_MAP_TCP :
					if (pkgsize != sizeof(natpmp_packet_map_request)) continue; /* TODO errorlog */
					handle_map_request(ufd_v[s_i].fd, &t_addr, &packet_request.map);
					break;
				default :
					handle_unsupported_request(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDOP);
					continue;
			}
		}
	}

	/* clean up */
	close_all();

	return 0;
}
