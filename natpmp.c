#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "natpmp_defs.h"

/* time this daemon has been started or tables got refreshed */
time_t timestamp;

/* list of socked file descriptors */
struct pollfd * sfd_v;
/* number of sockets */
int sfd_c;

/* close all sockets and free allocated memory */
void close_sockets() {
	int i;
	for (i=0; i<sfd_c; i++) {
		close(sfd_v[i].fd);
	}
	free(sfd_v);
}

/* function for clean dying */
void p_die(const char * p) {
	perror(p);
	close_sockets();
	exit(EXIT_FAILURE);
}

/* function for sending, if t_addr is given */
void udp_send_r(const int sfd, const struct sockaddr_in * t_addr, const void * data, const size_t len) {
	int err = sendto(sfd, data, len, MSG_DONTROUTE, (struct sockaddr *) t_addr, sizeof(*t_addr));
	if (err == -1) p_die("sendto");
}

#if 0
/* function for sending, if only destination address and port are given */
void udp_send(int sfd, in_addr_t address, in_port_t port, void * data, size_t len) {
	/* construct target socket address */
	struct sockaddr_in t_addr;
	memset(&t_addr, 0, sizeof(t_addr));
	t_addr.sin_family = AF_INET;
	t_addr.sin_port = htons(port);
	t_addr.sin_addr.s_addr = htonl(address);

	/* send it */
	udp_send_r(sfd, &t_addr, data, len);
}
#endif

/* return seconds since daemon started */
uint32_t get_epoch() {
	return time(NULL) - timestamp;
};

/* being called on unsupported requests */
void unsupported(const int sfd, const uint16_t result, const natpmp_packet_dummy_request * packet,
		const struct sockaddr_in * t_addr) {
	natpmp_packet_dummy_answer answer_packet;
	answer_packet.header.version = NATPMP_VERSION;
	/* it's not defined which op should be sent on undefined versions, so this should be ok, too */
	answer_packet.header.op = packet->header.op | NATPMP_ANSFLAG;
	answer_packet.answer.result = result;
	answer_packet.answer.epoch = get_epoch();
	udp_send_r(sfd, t_addr, &answer_packet, sizeof(answer_packet));
}

/* initialize and bind udp */
void udp_init(int * sfd, const char * listen_address, const in_port_t listen_port)
{
	/* create UDP socket */
	*sfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (*sfd == -1) p_die("socket");

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
		int err = bind(*sfd, (struct sockaddr *) &s_addr, sizeof(s_addr));
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
	sfd_c = 1;
	sfd_v = malloc(sfd_c * sizeof(*sfd_v));
	if (sfd_v == NULL) p_die("malloc");

	/* initialize sockets */
	{
		int i;
		for (i=0; i<sfd_c; i++); {
			udp_init(&sfd_v[i].fd, "0.0.0.0", NATPMP_PORT);

			/* prepare data structures for poll */
			sfd_v[i].events = POLLIN;
		}
	}

	/* fork into background */
	//fork_to_background();

	{
		/* socket index */
		int s_i = -1;

		/* main loop */
		while (1) {
			/* construct packet, where the received data ist stored to */
			natpmp_packet packet;
			memset(&packet, 0, sizeof(packet));

			/* there, the information of the sender are stored to */
			socklen_t t_len;
			struct sockaddr_in t_addr;
			memset(&t_addr, 0, sizeof(t_addr));

			/* wait until something is received */
			{
				int err = poll(sfd_v, sfd_c, -1);
				if (err == -1) p_die("poll");
			}

			ssize_t err;
			while (1) {
				if (++s_i >= sfd_c) s_i = 0;
				err = recvfrom(sfd_v[s_i].fd, &packet, sizeof(packet), MSG_DONTWAIT,
						(struct sockaddr *) &t_addr, &t_len);
				if (err == -1) p_die("recvfrom");
				if (err != EAGAIN && err != 0) break;
			}

			/* do things depending on the packet's content */
			if (err < sizeof(natpmp_packet_dummy_request)) continue; /* TODO errorlog */
			if (packet.header.version != NATPMP_VERSION)
				unsupported(sfd_v[s_i].fd, NATPMP_UNSUPPORTEDVERSION,
						(natpmp_packet_dummy_request *) &packet, &t_addr);
			if (packet.header.op & NATPMP_ANSFLAG) continue;
			switch (packet.header.op) {
				case NATPMP_PUBLICIPADDRESS :
					if (err != sizeof(natpmp_packet_publicipaddress_request)) continue; /* TODO errorlog */
					/* TODO */
					break;
				case NATPMP_MAP_UDP :
					if (err != sizeof(natpmp_packet_map_request)) continue; /* TODO errorlog */
					/* TODO */
					break;
				case NATPMP_MAP_TCP :
					if (err != sizeof(natpmp_packet_map_request)) continue; /* TODO errorlog */
					/* TODO */
					break;
				default :
					unsupported(sfd_v[s_i].fd, NATPMP_UNSUPPORTEDOP, (natpmp_packet_dummy_request *) &packet, &t_addr);
			}
		}
	}

	/* clean up */
	close_sockets();

	return 0;
}
