#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "natpmp_defs.h"

/* file descriptor for the udp socket */
int sfd;

/* function for clean dying */
void p_die(const char * p) {
	perror(p);
	close(sfd);
	exit(EXIT_FAILURE);
}

/* function for sending */
void udp_send(in_addr_t address, in_port_t port, void * data, size_t len) {
	/* construct target socket address */
	struct sockaddr_in t_addr;
	memset(&t_addr, 0, sizeof(t_addr));
	t_addr.sin_family = AF_INET;
	t_addr.sin_port = htons(port);
	t_addr.sin_addr.s_addr = htonl(address);

	/* send it */
	int err = sendto(sfd, data, len, MSG_DONTROUTE, (struct sockaddr *) &t_addr, sizeof(t_addr));
	if (err == -1) p_die("sendto");
}

int main() {
	/* create UDP socket */
	sfd = socket(PF_INET, SOCK_DGRAM, 0);
	if (sfd == -1) p_die("socket");

	/* define local address and port to bind to */
	struct sockaddr_in s_addr;
	memset(&s_addr, 0, sizeof(s_addr));
	s_addr.sin_family = AF_INET;
	s_addr.sin_port = htons(2039);
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

		/* do things depending on the op code */
		if (packet.header.op & NATPMP_ANSFLAG) continue;
		switch (packet.header.op & (~NATPMP_ANSFLAG)) {
			case NATPMP_PUBLICIPADDRESS :
				break;
			case NATPMP_MAP_UDP :
				break;
			case NATPMP_MAP_TCP :
				break;
			default :
				break;
		}
	}

	/* close socket */
	{
		int err = close(sfd);
		if (err == -1) p_die("close");
	}

	return 0;
}
