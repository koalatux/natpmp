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

#define PROGRAM_VERSION "0.1.1-devel"

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
#include <sys/time.h>
//#include <time.h>

#include "die.h"
#include "leases.h"
#include "dnat_api.h"
#include "natpmp_defs.h"

#define ADDRESS_CHECK_INTERVAL 1 /* s */

/* the file to write the PID to */
char * pidfile;

/* the name of the public interface */
char public_ifname[IFNAMSIZ];
/* maximum lifetime of a lease */
uint32_t max_lifetime;
/* the lowest port available for mapping */
uint16_t port_range_low;
/* the highest port available for mapping */
uint16_t port_range_high;
/* an offset added to ports lower than the allowed range */
uint16_t port_low_offset;

/* cache for the public ip address */
struct in_addr public_address;

/* time this daemon has been started or tables got refreshed */
uint32_t timestamp;

/* actual time in seconds and microseconds, updated in the main loop */
uint32_t now;
uint64_t unow;

/* list of leases */
extern lease * leases;
/* number of allocated leases */
extern int lease_a;
/* number of leases */
extern int lease_c;

/* time the next lease expires */
extern uint32_t next_lease_expires;
extern int update_expires;

/* list of socket file descriptors */
struct pollfd * ufd_v;
/* number of sockets */
int ufd_c;

/* multicast address for sending address changes to */
struct sockaddr_in multicast_address;

/* function for allocating memory */
void allocate_all() {
	/* allocate memory for leases */
	lease_c = 0;
	lease_a = ALLOCATE_AMOUNT;
	leases = malloc(ALLOCATE_AMOUNT * sizeof(*leases));
	if (leases == NULL) p_die("malloc");
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
	return htonl(now - timestamp);
};

/* function that returns local ip address of an interface */
struct in_addr get_ip_address(const char * ifname) {
	struct ifreq req;
	/* len is already checked at command line parsing */
	//if (strlen(ifname) >= IFNAMSIZ) die("get_ip_address: interface name too long");
	strncpy(req.ifr_ifrn.ifrn_name, ifname, IFNAMSIZ);
	int tfd = socket(PF_INET, SOCK_STREAM, 0);
	int err = ioctl(tfd, SIOCGIFADDR, &req);
	close(tfd);
	if (err == 0) {
		struct sockaddr_in * req_addr = (struct sockaddr_in *) &req.ifr_ifru.ifru_addr;
		return (struct in_addr) req_addr->sin_addr;
	}
	else {
		/* Return 0.0.0.0 on error */
		struct in_addr sin_addr;
		memset(&sin_addr, 0, sizeof(sin_addr));
		return sin_addr;
	}
}

/* send a NAT-PMP packet */
void send_natpmp_packet(const int ufd, const struct sockaddr_in * t_addr, natpmp_packet_answer * packet, size_t size) {
	packet->dummy.header.version = NATPMP_VERSION;
	packet->dummy.header.op |= NATPMP_ANSFLAG;
	packet->dummy.answer.epoch = get_epoch();
	udp_send_r(ufd, t_addr, packet, size);
}

/* check if something is listening on a given port, return 0 if port is free and -1 if not */
int is_port_free(const uint16_t port) {
	/* TODO: find a nicer alternative than trying to bind the ports (perhaps have a look at netstat) */

	/* creating the listen address */
	struct sockaddr_in s_addr;
	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = AF_INET;
	s_addr.sin_port = port;
	s_addr.sin_addr.s_addr = 0; /* all addresses */

	int try_bind(int type) {
		/* create socket */
		int fd = socket(PF_INET, type, 0);
		if (fd == -1) p_die("socket");
		/* bind to it */
		int err = bind(fd, (struct sockaddr *) &s_addr, sizeof(s_addr));
		close(fd);
		return err;
	}

	if (try_bind(SOCK_STREAM) == -1) return -1;
	if (try_bind(SOCK_DGRAM) == -1) return -1;
	return 0;
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
	answer_packet.mapping.public_port = request_packet->mapping.public_port;
	answer_packet.mapping.lifetime = request_packet->mapping.lifetime;

	if (answer_packet.mapping.lifetime) {
		/* creating a mapping is requested */

		if (ntohl(answer_packet.mapping.lifetime) > max_lifetime) {
			/* lifetime too high, downgrade */
			answer_packet.mapping.lifetime = htonl(max_lifetime);
		}

		lease * a = get_lease_by_client_port(client, answer_packet.mapping.private_port);
		if (a != NULL) {
			/* lease exists, update expiration time of the requested protocol and answer with public port */
			uint32_t new_expires = now + ntohl(answer_packet.mapping.lifetime);
			if (new_expires == next_lease_expires);
			else if (new_expires < next_lease_expires) next_lease_expires = new_expires;
			else if (a->expires[(int) protocol] <= next_lease_expires) update_expires = 1;
			a->expires[(int) protocol] = new_expires;
			answer_packet.mapping.public_port = a->public_port;
		}
		else {
			/* no lease exists, check for manual mapping */
			uint16_t public_port;
			int b = get_dnat_rule_by_client_port(protocol, &public_port, client, answer_packet.mapping.private_port);
			if (b == -1) die("get_dnat_rule_by_client_port returned with error");
			else if (b == 1) {
				/* manual mapping exists, answer with public port */
				answer_packet.mapping.public_port = public_port;
			}
			else {
				/* no lease and no manual mapping exist, find a valid port and create a lease */

				/* assure the port is not under the allowed range */
				if (ntohs(answer_packet.mapping.public_port) < port_range_low)
					answer_packet.mapping.public_port = htons(ntohs(answer_packet.mapping.public_port) + port_low_offset);
				/* catch overflows */
				if (ntohs(answer_packet.mapping.public_port) < port_range_low)
					answer_packet.mapping.public_port = htons(port_range_low);
				/* assure the port is not over the allowed range */
				if (ntohs(answer_packet.mapping.public_port) > port_range_high)
					answer_packet.mapping.public_port = htons(ntohs(answer_packet.mapping.public_port) %
							(port_range_high - port_range_low + 1) + port_range_low);

				/* find a free port */
				uint16_t try_count = 0;
				while (1) {
					if (try_count++ > port_range_high - port_range_low) {
						/* all ports checked, no free port found, restore variables and answer with out of resources */
						answer_packet.mapping.public_port = request_packet->mapping.public_port;
						answer_packet.mapping.lifetime = request_packet->mapping.lifetime;
						answer_packet.answer.result = NATPMP_OUTOFRESOURCES;
						break;
					}
					if (get_lease_by_port(answer_packet.mapping.public_port) == NULL &&
							is_port_free(answer_packet.mapping.public_port) == 0 &&
							get_dnat_rule_by_public_port(TCP, answer_packet.mapping.public_port, NULL, NULL) == 0 &&
							get_dnat_rule_by_public_port(UDP, answer_packet.mapping.public_port, NULL, NULL) == 0) {
						/* TODO: acquiring the companion port to a manual mapping can be allowed to the same client */
						/* these parameters are valid for mapping */

						/* add the lease to the database */
						{
							lease c;
							c.expires[UDP] = UINT32_MAX; c.expires[TCP] = UINT32_MAX;
							c.expires[(int) protocol] = now + ntohl(answer_packet.mapping.lifetime);
							c.client = client;
							c.private_port = answer_packet.mapping.private_port;
							c.public_port = answer_packet.mapping.public_port;
							if (c.expires[(int) protocol] < next_lease_expires) next_lease_expires = c.expires[(int) protocol];
							add_lease(&c);
						}

						/* create the mapping*/
						{
							int c = create_dnat_rule(
									protocol,
									answer_packet.mapping.public_port,
									client,
									answer_packet.mapping.private_port);
							if (c == -1) die("create_dnat_rule returned with error");
						}
						break;
					}

					if (ntohs(answer_packet.mapping.public_port) >= port_range_high) {
						answer_packet.mapping.public_port = htons(port_range_low);
					}
					else {
						answer_packet.mapping.public_port = htons(ntohs(answer_packet.mapping.public_port) + 1);
					}
				}
			}
		}
	}
	else {
		/* remove a mapping */
		lease * a;
		int remove_all;

		if (answer_packet.mapping.public_port == 0 && answer_packet.mapping.private_port == 0) {
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
				int b = destroy_dnat_rule(protocol, a->public_port, client, a->private_port);
				if (b == -1) die("destroy_dnat_rule returned with error");
				else if (b == 1) {
					/* mapping may not be destroyed, it's a manual mapping, answer with refused */
					answer_packet.answer.result = NATPMP_REFUSED;
				}

				/* update used protocols of lease */
				if (a->expires[(int) protocol] <= next_lease_expires) update_expires = 1;
				a->expires[(int) protocol] = UINT32_MAX;
				if (a->expires[UDP] == UINT32_MAX && a->expires[TCP] == UINT32_MAX) {
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
	natpmp_packet_publicipaddress_answer packet;
	packet.header.op = NATPMP_PUBLICIPADDRESS;
	if (public_address.s_addr != 0) packet.answer.result = NATPMP_SUCCESS;
	else packet.answer.result = NATPMP_NETFAILURE;
	packet.public_ip_address = public_address.s_addr;
	send_natpmp_packet(ufd, t_addr, (natpmp_packet_answer *) &packet, sizeof(packet));
}

/* initialize and bind udp */
void udp_init(int * ufd, const uint32_t listen_address, const uint16_t listen_port) {
	/* create UDP socket */
	*ufd = socket(PF_INET, SOCK_DGRAM, 0);
	if (*ufd == -1) p_die("socket");

	/* store local address and port to bind to */
	struct sockaddr_in s_addr;
	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = AF_INET;
	s_addr.sin_port = listen_port;
	s_addr.sin_addr.s_addr = listen_address;

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
		printf("forked into background -- %i\n", child);
		if (pidfile != NULL) {
			FILE * pidfilefd;
			pidfilefd = fopen(pidfile, "w");
			if (pidfilefd == NULL) {
				fprintf(stderr, "Could not write PID to file %s.\n", pidfile);
			}
			else {
				fprintf(pidfilefd, "%i\n", child);
				fclose(pidfilefd);
			}
		}
		exit(EXIT_SUCCESS);
	}
}

void read_from_socket(const int s_i) {
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
		/* Apple's Airport stations send an opcode of 0 here, but it's not defined in the draft,
		 * perhaps this could be used for fingerprinting my implementation :-) */
		handle_unsupported_request(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDVERSION);
		return;
	}
	if (packet_request.dummy.header.op & NATPMP_ANSFLAG) return;

	/* do things depending on the packet's op code */
	switch (packet_request.dummy.header.op) {
		case NATPMP_PUBLICIPADDRESS :
			if (pkgsize < (ssize_t) sizeof(natpmp_packet_publicipaddress_request)) return; /* TODO: errorlog */
			send_publicipaddress(ufd_v[s_i].fd, &t_addr);
			break;
		case NATPMP_MAP_UDP :
		case NATPMP_MAP_TCP :
			if (pkgsize < (ssize_t) sizeof(natpmp_packet_map_request)) return; /* TODO: errorlog */
			handle_map_request(ufd_v[s_i].fd, &t_addr, &packet_request.map);
			break;
		default :
			handle_unsupported_request(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDOP);
	}
}

void update_time() {
	struct timeval a;
	gettimeofday(&a, NULL);
	now = a.tv_sec;
	unow = a.tv_usec + 1000000 * now;
}

//__attribute__ ((noreturn))
void print_usage(const char * program_name) {
	fprintf(stderr, "Usage: %s [-b] -i interface -a address [-a address [...]] -- backend-options\n", program_name);
	exit(EXIT_FAILURE);
}

void do_version() {
	printf(
			"natpmp version " PROGRAM_VERSION ".\n\n"

			"Copyright (C) 2007  Adrian Friedli.\n\n"

			"This program is free software; you can redistribute it and/or modify\n"
			"it under the terms of the GNU General Public License as published by\n"
			"the Free Software Foundation; either version 2 of the License, or\n"
			"(at your option) any later version.\n\n"

			"This program is distributed in the hope that it will be useful,\n"
			"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
			"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
			"GNU General Public License for more details.\n\n"

			"You should have received a copy of the GNU General Public License along\n"
			"with this program; if not, write to the Free Software Foundation, Inc.,\n"
			"51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.\n"
	      );
}

void print_public_ip_address() {
	if (public_address.s_addr != 0) {
		printf("IP address of %s: %s\n", public_ifname, inet_ntoa(public_address));
	}
	else {
		printf("IP address of %s: none\n", public_ifname);
	}
}

void init(int argc, char * argv[]) {
	int do_fork = 0;

	/* set defaults */
	max_lifetime = NATPMP_RECOMMENDED_LIFETIME;
	port_range_low = 1024; /* ports below 1024 are restricted ports */
	port_range_high = 65535; /* 65535 is the highest port available */

#define OPTSTRING "Vbp:i:a:t:l:u:"
	/* parse the command line */
	{
		extern char *optarg;
		extern int optind, opterr, optopt;
		int opt;

		/* get number of addresses to bind to */
		ufd_c = 0;
		opterr = 0;
		while ( (opt = getopt(argc, argv, OPTSTRING)) != -1 ) {
			switch (opt) {
				case 'a':
					ufd_c++;
					break;
				case 'V':
					do_version();
					exit(EXIT_SUCCESS);
					break;
			}
		}

		/* allocate memory for sockets */
		ufd_v = malloc(ufd_c * sizeof(*ufd_v));
		if (ufd_v == NULL) p_die("malloc");
		struct in_addr * laddresses = malloc(ufd_c * sizeof(*laddresses));
		if (laddresses == NULL) p_die("malloc");

		/* parse the command line */
		int i = 0;
		optind = 0;
		opterr = -1;
		pidfile = NULL;
		memset(public_ifname, 0, sizeof(public_ifname));
		while ( (opt = getopt(argc, argv, OPTSTRING)) != -1 ) {
			/* check for nullstrings in the arguments */
			switch (opt) {
				case 'i':
				case 'a':
				case 't':
					if (optarg[0] == '\0') {
						fprintf(stderr, "%s: argument %i is a bit too short.\n", argv[0], optind - 1);
						print_usage(argv[0]);
					}
					break;
			}

			/* handle the options */
			switch (opt) {
				case 'b': /* fork to background */
					do_fork = -1;
					break;
				case 'p': /* PID file */
					pidfile = optarg;
					break;
				case 'i': /* public interface name */
					if (strlen(optarg) >= IFNAMSIZ) {
						fprintf(stderr, "%s: argument %i is too long for a valid interface name.\n", argv[0], optind - 1);
						print_usage(argv[0]);
					}
					strncpy(public_ifname, optarg, IFNAMSIZ);
					break;
				case 'a': /* address to listen on */
					if (inet_aton(optarg, &laddresses[i]) == 0) {
						fprintf(stderr, "%s: argument %i is not a valid ip address.\n", argv[0], optind - 1);
						print_usage(argv[0]);
					}
					i++;
					break;
				case 't': /* maximal lifetime */
					max_lifetime = atol(optarg);
					if (max_lifetime == 0) {
						fprintf(stderr, "%s: argument %i is not a valid lifetime.\n", argv[0], optind - 1);
						print_usage(argv[0]);
					}
					break;
				case 'l': /* lowest port number allowed */
					port_range_low = atoi(optarg);
					if (port_range_low == 0) {
						fprintf(stderr, "%s: argument %i is not a valid port number.\n", argv[0], optind - 1);
						print_usage(argv[0]);
					}
					break;
				case 'u': /* highest port number allowed */
					port_range_high = atoi(optarg);
					if (port_range_high == 0) {
						fprintf(stderr, "%s: argument %i is not a valid port number.\n", argv[0], optind - 1);
						print_usage(argv[0]);
					}
					break;
				default: /* invalid option */
					fprintf(stderr, "%s: argument %i is invalid.\n", argv[0], optind - 1);
					print_usage(argv[0]);
			}
		}

		if (public_ifname[0] == 0) {
			fprintf(stderr, "%s: option required -- i\n", argv[0]);
			print_usage(argv[0]);
		}

		if (ufd_c == 0) {
			fprintf(stderr, "%s: option required -- a\n", argv[0]);
			print_usage(argv[0]);
		}

		/* port_range_low <= port_range_high; else bahaviour is undefined
		 * port_range_high <= 65535; else range not guaranteed; given through 16 bit integer */
		if (port_range_high < port_range_low) {
			fprintf(stderr, "%s: lower port may not be smaller than upper port. \n", argv[0]);
			print_usage(argv[0]);
		}

		/* port_low_offset >= port_range_low; else range not guaranteed */
		port_low_offset = (port_range_low / 1000 + 1) * 1000;
		/* catch overflows */
		if (port_low_offset < port_range_low) port_low_offset = port_range_low;

		printf("Allowed port range: %d..%d, maximal lifetime: %d\n", port_range_low, port_range_high, max_lifetime);
		if (max_lifetime < NATPMP_RECOMMENDED_LIFETIME)
			fprintf(stderr, "Warning: using maximal lifetime lower than recommended value %d\n", NATPMP_RECOMMENDED_LIFETIME);

		public_address = get_ip_address(public_ifname);
		print_public_ip_address();

		/* initialize sockets */
		for (i=0; i<ufd_c; i++) {
			udp_init(&ufd_v[i].fd, laddresses[i].s_addr, NATPMP_PORT);

			/* prepare data structures for poll */
			ufd_v[i].events = POLLIN;

			if (laddresses[i].s_addr != 0) printf("Listening on %s\n", inet_ntoa(laddresses[i]));
			else fprintf(stderr, "Warning: Listening on 0.0.0.0 is not a good idea\n");
		}

		free(laddresses);

		dnat_init(argc - optind, &argv[optind]);
	}

	/* fork into background, must be called before registering atexit functions */
	if (do_fork) fork_to_background();

	/* register functions being called on exit() */
	{
		int err = atexit(close_all);
		if (err) die("atexit returned with error");
	}

	/* set timestamp */
	update_time();
	timestamp = now; /* TODO: move to where tables get (re)loaded */

	/* allocate some memory and set some variables */
	allocate_all();

	/* fill out the multicast address for sending address changes to */
	memset(&multicast_address, 0, sizeof(multicast_address));
	multicast_address.sin_family = AF_INET;
	multicast_address.sin_port = NATPMP_PORT;
	multicast_address.sin_addr.s_addr = NATPMP_MULTICAST_ADDRESS;
}

int main(int argc, char * argv[]) {
#if 0
	/* test create_dnat_rule() XXX */
	{
		struct in_addr address;
		inet_aton("192.168.1.2", &address);
		create_dnat_rule(1, htons(81), address.s_addr, htons(80));
		destroy_dnat_rule(2, htons(81), address.s_addr, htons(80));
	}
#endif

	init(argc, argv);

	uint32_t next_address_check = now;
	uint64_t next_announce_send = UINT64_MAX;
	int announce_count = 0;

	/* main loop */
	while (42) {
		/* wait until something's got received or we have to do somehing else */
		int pollret;
		{
			int timeout1 = (next_address_check - now) * 1000;
			if (next_announce_send != UINT64_MAX) {
				int timeout2 = (next_announce_send - unow) / 1000;
				if (timeout2 < timeout1) timeout1 = timeout2;
			}
			if (next_lease_expires != UINT32_MAX) {
				int timeout3 = (next_lease_expires - now) * 1000;
				if (timeout3 < timeout1) timeout1 = timeout3;
			}

			pollret = poll(ufd_v, ufd_c, timeout1);
			if (pollret == -1) p_die("poll");
		}

		/* update the current time */
		update_time();

		/* check for public ip address change */
		if (next_address_check <= now) {
			next_address_check = now + ADDRESS_CHECK_INTERVAL;
			struct in_addr address = get_ip_address(public_ifname);
			if (address.s_addr != public_address.s_addr) {
				public_address = address;
				print_public_ip_address();
				next_announce_send = unow;
				announce_count = 0;
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

		/* announce the public ip address on change */
		if (next_announce_send <= unow) {
			if (public_address.s_addr != 0) {
				int i;
				for (i=0; i<ufd_c; i++) {
					send_publicipaddress(ufd_v[i].fd, &multicast_address);
				}
			}

			if (announce_count + 1 < NATPMP_ANNOUNCE_PACKETS && public_address.s_addr != 0) {
				next_announce_send = unow + (1 << announce_count) * NATPMP_ADDRESS_ANNOUNCE_INTERVAL;
				announce_count++;
			}
			else {
				next_announce_send = UINT64_MAX;
				announce_count = 0;
			}
		}

		/* destroy expired mappings */
		if (next_lease_expires <= now) {
			lease * a;
			while ((a = get_next_expired_lease(now, NULL)) != NULL) {
				/* local function that destroys the mapping */
				void destroy_expired(const char protocol) {
					if (a->expires[(int) protocol] <= now) {
						a->expires[(int) protocol] = UINT32_MAX;
						int b = destroy_dnat_rule(protocol, a->public_port, a->client, a->private_port);
						if (b == -1) die("destroy_dnat_rule returned with error");
					}
				}

				destroy_expired(UDP);
				destroy_expired(TCP);

				if (a->expires[UDP] == UINT32_MAX && a->expires[TCP] == UINT32_MAX) {
					/* lease is no more used, remove it */
					remove_lease_by_pointer(a);
				}
			}
		}

		/* update_expires if it hasn't been done by get_next_expired_lease */
		if (update_expires) {
			do_update_expires();
			update_expires = 0;
		}
	}
}
