#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "natpmp_defs.h"

/* file descriptor for the udp socket */
int sfd;

/* time this daemon has been started or tables got refreshed */
time_t timestamp;

/* function for clean dying */
void p_die(const char * p) {
	perror(p);
	close(sfd);
	exit(EXIT_FAILURE);
}

/* function for sending, if t_addr is given */
void udp_send_r(struct sockaddr_in * t_addr, void * data, size_t len) {
	int err = sendto(sfd, data, len, MSG_DONTROUTE, (struct sockaddr *) t_addr, sizeof(*t_addr));
	if (err == -1) p_die("sendto");
}

/* function for sending, if only destination address and port are given */
void udp_send(in_addr_t address, in_port_t port, void * data, size_t len) {
	/* construct target socket address */
	struct sockaddr_in t_addr;
	memset(&t_addr, 0, sizeof(t_addr));
	t_addr.sin_family = AF_INET;
	t_addr.sin_port = htons(port);
	t_addr.sin_addr.s_addr = htonl(address);

	/* send it */
	udp_send_r(&t_addr, data, len);
}

/* return seconds since daemon started */
uint32_t get_epoch() {
	return time(NULL) - timestamp;
};

/* being called on unsupported functions */
void unsupported(uint16_t result, natpmp_packet_dummy_request * packet, struct sockaddr_in * t_addr) {
	natpmp_packet_dummy_answer answer_packet;
	answer_packet.header.version = NATPMP_VERSION;
	/* it's not defined which op should be sent on undefined versions, so this should be ok, too */
	answer_packet.header.op = packet->header.op | NATPMP_ANSFLAG;
	answer_packet.answer.result = result;
	answer_packet.answer.epoch = get_epoch();
	udp_send_r(t_addr, &answer_packet, sizeof(answer_packet));
}

int main() {
	/* set timestamp TODO: move to where tables get (re)loaded */
	timestamp = time(NULL);

	/* create UDP socket */
	sfd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sfd == -1) p_die("socket");

	/* define local address and port to bind to */
	struct sockaddr_in s_addr;
	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(NATPMP_PORT);
	s_addr.sin_addr.s_addr = 0;

	/* bind here */
	{
		int err = bind(sfd, (struct sockaddr *) &s_addr, sizeof(s_addr));
		if (err == -1) p_die("bind");
	}

	/* udp_send(0x7f000001, 2000, "hoi", 3); */

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
		ssize_t err = recvfrom(sfd, &packet, sizeof(packet), 0, (struct sockaddr *) &t_addr, &t_len);
		if (err == -1) p_die("recvfrom");
		else if (err == 0) continue;

		/* do things depending on the packet content */
		if (err < sizeof(natpmp_packet_dummy_request)) continue; /* TODO errorlog */
		if (packet.header.version != NATPMP_VERSION)
			unsupported(NATPMP_UNSUPPORTEDVERSION, (natpmp_packet_dummy_request *) &packet, &t_addr);
		if (packet.header.op & NATPMP_ANSFLAG) continue; /* TODO errorlog */
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
				unsupported(NATPMP_UNSUPPORTEDOP, (natpmp_packet_dummy_request *) &packet, &t_addr);
		}
	}

	/* close socket */
	{
		int err = close(sfd);
		if (err == -1) p_die("close");
	}

	return 0;
}
