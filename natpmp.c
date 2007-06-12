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

#define POLL_TIMEOUT 250 /* ms */

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

/* cache for the public ip address */
struct in_addr public_address;

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
	leases = malloc(ALLOCATE_AMOUNT * sizeof(*leases));
	if (leases == NULL) p_die("malloc");

	/* allocate memory for sockets */
	ufd_c = 1;
	ufd_v = malloc(ufd_c * sizeof(*ufd_v));
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
	int err = sendto(fd, data, len, MSG_DONTROUTE, (struct sockaddr *) t_addr, sizeof(*t_addr));
	if (err == -1) p_die("sendto");
}

/* return seconds since daemon started or tables got refreshed */
uint32_t get_epoch() {
	return htonl(time(NULL) - timestamp);
};

/* function that returns local ip address of an interface */
struct in_addr get_ip_address(const char * ifname) {
	struct ifreq req;
	if (strlen(ifname) >= IFNAMSIZ) die("get_ip_address: interface name too long");
	strncpy(req.ifr_ifrn.ifrn_name, ifname, IFNAMSIZ);
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

	if (answer_packet.mapping.lifetime) {
		/* creating a mapping is requested */

		if (answer_packet.mapping.lifetime > MAX_LIFETIME) {
			/* lifetime too high, downgrade */
			answer_packet.mapping.lifetime = MAX_LIFETIME;
		}

		lease * a = get_lease_by_client_port(client, answer_packet.mapping.private_port);
		if (a != NULL) {
			/* lease exists, update expiration time of the requested protocol and answer with mapped port */
			a->expires[(int) protocol] = time(NULL) + ntohl(answer_packet.mapping.lifetime);
			answer_packet.mapping.mapped_port = a->mapped_port;
		}
		else {
			/* no lease exists, check for manual mapping */
			uint16_t mapped_port;
			int b = get_dnat_rule_by_client_port(protocol, &mapped_port, client, answer_packet.mapping.private_port);
			if (b == -1) die("get_dnat_rule_by_client_port returned with error");
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
							c.expires[UDP] = 0; c.expires[TCP] = 0;
							c.expires[(int) protocol] = time(NULL) + ntohl(answer_packet.mapping.lifetime);
							c.client = client;
							c.private_port = answer_packet.mapping.private_port;
							c.mapped_port = answer_packet.mapping.mapped_port;
							add_lease(&c);
						}

						/* create the mapping*/
						{
							int c = create_dnat_rule(
									protocol,
									answer_packet.mapping.mapped_port,
									client,
									answer_packet.mapping.private_port);
							if (c == -1) die("create_dnat_rule returned with error");
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

		while (remove_all == 0 || (a = get_next_lease_by_client(client, NULL)) != NULL) {
			if (a != NULL) {
				/* destroy mapping */
				int b = destroy_dnat_rule(protocol, a->mapped_port, client, a->private_port);
				if (b == -1) die("destroy_dnat_rule returned with error");
				else if (b == 1) {
					/* mapping may not be destroyed, it's a manual mapping, answer with refused */
					answer_packet.answer.result = NATPMP_REFUSED;
				}

				/* update used protocols of lease */
				a->expires[(int) protocol] = 0;
				if (a->expires[UDP] == 0 && a->expires[TCP] == 0) {
					/* lease is no more used, remove it */
					remove_lease_by_pointer(a);
				}
			}
			else {
				/* lease not found, check for manual mapping */
				int b = get_dnat_rule_by_client_port(protocol, NULL, client, answer_packet.mapping.private_port);
				if (b == -1) die("get_dnat_rule_by_client_port returned with error");
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
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &answer_packet, sizeof(answer_packet));
}

/* being called on unsupported requests */
void handle_unsupported_request(const int ufd, const struct sockaddr_in * t_addr, const natpmp_packet_dummy_request * request_packet, const uint16_t result) {
	natpmp_packet_dummy_answer answer_packet;
	answer_packet.header.op = request_packet->header.op;
	answer_packet.answer.result = result;
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &answer_packet, sizeof(answer_packet));
}

/* answer with public IP address */
void send_publicipaddress(const int ufd, const struct sockaddr_in * t_addr) {
	/* TODO: answer with Network Failure if no ip address found on the public port */
	natpmp_packet_publicipaddress_answer packet;
	packet.header.op = NATPMP_PUBLICIPADDRESS;
	packet.answer.result = NATPMP_SUCCESS;
	packet.public_ip_address = public_address.s_addr;
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &packet, sizeof(packet));
}

/* initialize and bind udp */
void udp_init(int * ufd, const char * listen_address, const uint16_t listen_port) {
	/* create UDP socket */
	//*ufd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	*ufd = socket(PF_INET, SOCK_DGRAM, 0);
	if (*ufd == -1) p_die("socket");

	/* store local address and port to bind to */
	struct sockaddr_in s_addr;
	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(listen_port);
	{
		int err = inet_aton(listen_address, &s_addr.sin_addr);
		if (err == 0) p_die("inet_aton");
	}

	/* bind here */
	{
		int err = bind(*ufd, (struct sockaddr *) &s_addr, sizeof(s_addr));
		if (err == -1) p_die("bind");
	}
}

/* function to fork to background and exit parent */
void fork_to_background() {
	pid_t child = fork();
	if (child == -1) p_die("fork");
	else if (child) {
		fprintf(stderr, "Forked into background.\n");
		printf("%i\n", child);
		exit(EXIT_SUCCESS);
	}
}

void read_from_socket(int s_i) {
	/* construct packet, where the received data ist stored to */
	natpmp_packet_request packet_request;
	memset(&packet_request, 0, sizeof(packet_request));

	/* construct struct, where information about the sender is stored to */
	struct sockaddr_in t_addr;
	memset(&t_addr, 0, sizeof(t_addr));

	ssize_t pkgsize;
	{
		socklen_t t_len = sizeof(t_addr);
		pkgsize = recvfrom(ufd_v[s_i].fd, &packet_request, sizeof(packet_request), MSG_DONTWAIT | MSG_TRUNC,
				(struct sockaddr *) &t_addr, &t_len);
		if (pkgsize == -1) p_die("recvfrom");
		if (t_len != sizeof(t_addr)) die("pkgsize returned invalid from address len");
	}
	if (pkgsize == EAGAIN) return;

	/* check for wrong or unsupported packets */
	if (pkgsize < (ssize_t) sizeof(natpmp_packet_dummy_request)) return; /* TODO: errorlog */
	if (packet_request.dummy.header.version != NATPMP_VERSION) {
		handle_unsupported_request(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDVERSION);
		return;
	}
	if (packet_request.dummy.header.op & NATPMP_ANSFLAG) return;

	/* do things depending on the packet's op code */
	switch (packet_request.dummy.header.op) {
		case NATPMP_PUBLICIPADDRESS :
			if (pkgsize != sizeof(natpmp_packet_publicipaddress_request)) return; /* TODO: errorlog */
			send_publicipaddress(ufd_v[s_i].fd, &t_addr);
			break;
		case NATPMP_MAP_UDP :
		case NATPMP_MAP_TCP :
			if (pkgsize != sizeof(natpmp_packet_map_request)) return; /* TODO: errorlog */
			handle_map_request(ufd_v[s_i].fd, &t_addr, &packet_request.map);
			break;
		default :
			handle_unsupported_request(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDOP);
	}
}

void init() {
	/* fork into background, must be called before registering atexit functions */
	//fork_to_background();

	/* register functions being called on exit() */
	{
		int err = atexit(close_all);
		if (err) die("atexit returned with error");
	}

	/* set timestamp */
	timestamp = time(NULL); /* TODO: move to where tables get (re)loaded */

	/* allocate some memory and set some variables */
	allocate_all();

	/* initialize sockets */
	{
		int i;
		for (i=0; i<ufd_c; i++) {
			udp_init(&ufd_v[i].fd, "0.0.0.0", NATPMP_PORT);

			/* prepare data structures for poll */
			ufd_v[i].events = POLLIN;
		}
	}

	public_address = get_ip_address(PUBLIC_IFNAME);
}

int main() {
#if 0
	/* test create_dnat_rule() XXX */
	{
		struct in_addr address;
		inet_aton("192.168.1.2", &address);
		create_dnat_rule(1, htons(81), address.s_addr, htons(80));
		create_dnat_rule(2, htons(81), address.s_addr, htons(80));
	}
#endif

	init();

	fprintf(stderr, "IP address of %s: %s\n", PUBLIC_IFNAME, inet_ntoa(public_address));

	uint32_t last_run = 0;

	/* main loop */
	while (42) {
		/* wait until something's got received or time */
		int pollret = poll(ufd_v, ufd_c, POLL_TIMEOUT);
		if (pollret == -1) p_die("poll");

		/* check for public ip address change */
		{
			struct in_addr address = get_ip_address(PUBLIC_IFNAME);
			if (address.s_addr != public_address.s_addr) {
				public_address = address;
				/* TODO: announce new address */
			}
		}

		/* check the sockets if something's got received */
		{
			int i;
			for (i=0; pollret>0; i++) {
				if (ufd_v[i].revents) {
					if (ufd_v[i].revents == POLLIN) read_from_socket(i);
					/* TODO: handle the three error types differently */
					else die("a socket file descriptor caused an error");
					pollret--;
				}
			}
		}

		/* destroy expired mappings */
		{
			uint32_t now = time(NULL);
			if (now != last_run) {
				last_run = now;
				lease * a;
				while ((a = get_next_expired_lease(now, NULL)) != NULL) {
					/* local function that destroys the mapping */
					void destroy_expired(const char protocol) {
						if (a->expires[(int) protocol] <= now && a->expires[(int) protocol]) {
							a->expires[(int) protocol] = 0;
							int b = destroy_dnat_rule(protocol, a->mapped_port, a->client, a->private_port);
							if (b == -1) die("destroy_dnat_rule returned with error");
						}
					}

					destroy_expired(UDP);
					destroy_expired(TCP);

					if (a->expires[UDP] == 0 && a->expires[TCP] == 0) {
						/* lease is no more used, remove it */
						remove_lease_by_pointer(a);
					}
				}
			}
		}
	}
}
