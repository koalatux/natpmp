/*
 *    natpmp - an implementation of NAT-PMP
 *    Copyright (C) 2007  Adrian Friedli
 *		(C) 2007  Simon Neininger
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

/*
 * ip addresses and port numbers all are in network byte order!
 * create_dnat_rule() and destroy_dnat_rule() must succeed if rule was already there respectively has already gone.
 * port numbers in a rule with a port range must be considered as existing but must not get destroyed,
 * destroy_dnat_rule() must return 1 if requested to do so.
 */

const char* proto(const char protocol)
{
  if(protocol == UDP)
    return "udp";
    //strcpy(proto,"udp");
  if(protocol == TCP)
      //strcpy(proto,"tcp");
      return "tcp";
  die("Wrong Protocol");
  return 0;
}
  
  
/* search DNAT rule for given mapped port, return 1 if found, 0 if not found and -1 on error, set client and private_port if not NULL */
int get_dnat_rule_by_mapped_port(const char protocol, const uint16_t mapped_port, uint32_t * client, uint16_t * private_port)
{
  return -1;
}

/* search DNAT rule for given private port and client, return 1 if found, 0 if not found and -1 on error, set mapped_port if not NULL */
int get_dnat_rule_by_client_port(const char protocol, uint16_t * mapped_port, const uint32_t client, const uint16_t private_port)
{
  return -1;
}

/* create a DNAT rule with given mapped port, client ip address and private port, return 0 on success and -1 on failure */
int create_dnat_rule(const char protocol, const uint16_t mapped_port, const uint32_t client, const uint16_t private_port)
{
  //iptables -t nat -A natpmp -p tcp --dport 8010  -j DNAT --to-destination 192.168.2.16

  static char command[255];
  struct in_addr client_addr;
  client_addr.s_addr=client;

  snprintf(command,sizeof(command),
    "echo iptables -t nat -A natpmp -p %s --dport %d -j DNAT --to-destination %s:%d",
    proto(protocol),mapped_port,inet_ntoa(client_addr), private_port);

  if(system(command))
    return -1;
  return 0;  
}

/* destroy a DNAT rule with given mapped port, client ip address and private port, return 0 on success, 1 when forbidden and -1 on failure */
int destroy_dnat_rule(const char protocol, const uint16_t mapped_port, const uint32_t client, const uint16_t private_port)
{
  return 1;
}

