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
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <poll.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "natpmp_defs.h"

#define PUBLIC_IFNAME "eth0"

/* time this daemon has been started or tables got refreshed */
time_t timestamp;

/* list of socked file descriptors */
struct pollfd * ufd_v;
/* number of sockets */
int ufd_c;

/* close all sockets and free allocated memory */
void close_sockets() {
	int i;
	for (i=0; i<ufd_c; i++) {
		close(ufd_v[i].fd);
	}
	free(ufd_v);
}

/* functions for clean dying */
void die(const char * e) {
	fprintf(stderr, "%s\n", e);
	close_sockets();
	exit(EXIT_FAILURE);
}

void p_die(const char * p) {
	perror(p);
	close_sockets();
	exit(EXIT_FAILURE);
}

/* function for sending, if t_addr is given */
void udp_send_r(const int fd, const struct sockaddr_in * t_addr, const void * data, const size_t len) {
	int err = sendto(fd, data, len, MSG_DONTROUTE, (struct sockaddr *) t_addr, sizeof(struct sockaddr_in));
	if (err == -1) p_die("sendto");
}

#if 0
/* function for sending, if only destination address and port are given */
void udp_send(int ufd, uint32_t address, uint16_t port, void * data, size_t len) {
	/* construct target socket address */
	struct sockaddr_in t_addr;
	memset(&t_addr, 0, sizeof(struct sockaddr_in));
	t_addr.sin_family = AF_INET;
	t_addr.sin_port = htons(port);
	t_addr.sin_addr.s_addr = htonl(address);

	/* send it */
	udp_send_r(ufd, &t_addr, data, len);
}
#endif

/* return seconds since daemon started */
uint32_t get_epoch() {
	return time(NULL) - timestamp;
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

/* being called on unsupported requests */
void unsupported(const int ufd, const struct sockaddr_in * t_addr, const natpmp_packet_dummy_request * request_packet, const uint16_t result) {
	natpmp_packet_answer answer_packet;
	answer_packet.dummy.header.op = request_packet->header.op;
	answer_packet.dummy.answer.result = result;
	send_natpmp_packet(ufd, t_addr, &answer_packet, sizeof(natpmp_packet_dummy_answer));
}

/* answer with public IP address */
void send_publicipaddress(const int ufd, const struct sockaddr_in * t_addr) {
	natpmp_packet_answer packet;
	packet.publicipaddress.header.op = NATPMP_PUBLICIPADDRESS;
	packet.publicipaddress.answer.result = NATPMP_SUCCESS;
	packet.publicipaddress.public_ip_address = get_ip_address(PUBLIC_IFNAME).s_addr;
	send_natpmp_packet(ufd, t_addr, &packet, sizeof(natpmp_packet_publicipaddress_answer));
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

#if 0
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
#endif

int main() {
	/* set timestamp TODO move to where tables get (re)loaded */
	timestamp = time(NULL);

	/* allocate memory for sockets */
	ufd_c = 1;
	ufd_v = malloc(ufd_c * sizeof(struct pollfd));
	if (ufd_v == NULL) p_die("malloc");

	/* initialize sockets */
	{
		int i;
		for (i=0; i<ufd_c; i++) {
			udp_init(&ufd_v[i].fd, "0.0.0.0", NATPMP_PORT);

			/* prepare data structures for poll */
			ufd_v[i].events = POLLIN;
		}
	}

	printf("IP address of %s: %s\n", PUBLIC_IFNAME, inet_ntoa(get_ip_address(PUBLIC_IFNAME)));

	/* fork into background */
	//fork_to_background();

	{
		/* socket index */
		int s_i = -1;

		/* main loop */
		while (1) {
			/* construct packet, where the received data ist stored to */
			natpmp_packet_request packet_request;
			memset(&packet_request, 0, sizeof(natpmp_packet_request));

			/* there, the information of the sender are stored to */
			socklen_t t_len;
			struct sockaddr_in t_addr;
			memset(&t_addr, 0, sizeof(struct sockaddr_in));

			/* wait until something is received */
			{
				int err = poll(ufd_v, ufd_c, -1);
				if (err == -1) p_die("poll");
			}

			ssize_t pkgsize;
			while (1) {
				if (++s_i >= ufd_c) s_i = 0;
				pkgsize = recvfrom(ufd_v[s_i].fd, &packet_request, sizeof(natpmp_packet_request), MSG_DONTWAIT,
						(struct sockaddr *) &t_addr, &t_len);
				if (pkgsize == -1) p_die("recvfrom");
				if (pkgsize != EAGAIN && pkgsize != 0) break;
			}

			/* check for wrong or unsupported packets */
			if (pkgsize < sizeof(natpmp_packet_dummy_request)) continue; /* TODO errorlog */
			if (packet_request.dummy.header.version != NATPMP_VERSION) {
				unsupported(ufd_v[s_i].fd, &t_addr, &packet_request.dummy, NATPMP_UNSUPPORTEDVERSION);
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
					if (pkgsize != sizeof(natpmp_packet_map_request)) continue; /* TODO errorlog */
					//packet_size = sizeof(natpmp_packet_map_answer);
					/* TODO */
					break;
				case NATPMP_MAP_TCP :
					if (pkgsize != sizeof(natpmp_packet_map_request)) continue; /* TODO errorlog */
					//packet_size = sizeof(natpmp_packet_map_answer);
					/* TODO */
					break;
				default :
					unsupported(ufd_v[s_i].fd, &t_addr, (natpmp_packet_dummy_request *) &packet_request, NATPMP_UNSUPPORTEDOP);
					continue;
			}
		}
	}

	/* clean up */
	close_sockets();

	return 0;
}
