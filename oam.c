// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "configfile.h"
#include "oam.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "if_utils.h"
#include "interface.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>

int nr_oam_ifaces = 0;
struct Interface *oam_ifaces[16];
struct Interface *oam_cmd_iface = NULL;

unsigned cmd_id = 1000;

int oam_ping(unsigned id, char *stream, char *mep_start, char *mep_stop, int level){
  printf("OAM ping id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);

  /* TODO : remove, just for  testing ->  */

  char msg[]="This is an OAM reply test message.\n\0";

  // get OAM dest IP */
  char addr[INET_ADDRSTRLEN];
  struct sockaddr_in saddr;
  struct Value ip = {&saddr.sin_addr, 0, 32};
  if(nr_oam_ifaces > 0){
    value_producer *read = oam_ifaces[0]->get_property_reader(oam_ifaces[0], "ip", FT_IPV4ADDRESS, &ip);
    if (read == NULL) {
      printf("interface %s has no property named 'ip'", oam_ifaces[0]->name);
    }
  }else{
    fprintf(stderr, "no oam interface configured.\n");
    return -1;
  }
  inet_ntop(AF_INET, &saddr.sin_addr, addr, INET_ADDRSTRLEN);

  oam_send_reply(addr, msg);
  /*   <- testing   */


  return 0;
}

int oam_trace(unsigned id, char *stream, char *mep_start, char *mep_stop, int level){
  printf("OAM trace id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);
  return 0;
}

int oam_discovery(unsigned id, char *stream, char *mep_start, char *mep_stop, int level){
  printf("OAM discovery id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);
  return 0;
}

/*
 * Send UDP OAM reply mesage
 * Address: destination address string (can be either IPV4, IPv6, or FQDN)
 * Msg: pointer to the message
 * Return 0 on success
*/
int oam_send_reply(char *address, char *msg){

  struct addrinfo hints, *res, *rp;
  int status;

  char port_str[15];
  sprintf(port_str, "%u", OAM_PORT);
  bzero(&hints, sizeof(hints));
  hints.ai_family = AF_INET; // AF_INET to force version
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  if ((status = getaddrinfo(address, port_str, &hints, &res)) != 0) {
    fprintf(stderr, "oam_send_reply getaddrinfo for address '%s': %s\n", address, gai_strerror(status));
    return -1;
  }

  int sock = -1;
  for (rp=res; rp!=NULL; rp=rp->ai_next) {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sock < 0) continue;

    if (sendto(sock, msg, strlen(msg), 0, rp->ai_addr, rp->ai_addrlen) == 0) {
        perror("oam repy sendto");
        freeaddrinfo(res);
        close(sock);
        return -1;
    }
  }

  freeaddrinfo(res);
  close(sock);
  return 0;
}

// Initialize OAM functionality
bool init_oam(struct R2d2Config *config){
  (void)config;
  printf("Init OAM fuctionality.\n");

  /*  - not needed, init from config
  unsigned port = OAM_CMD_PORT;
  unsigned ver = 4;
  if (!init_oam_cmd_interface(&config->ifaces[config->ifcount], "OAM-CMD", NULL, "0.0.0.0", port, ver)) {
      printf("failed to create oam interface");
  }

  struct Interface *cmd_iface = &config->ifaces[config->ifcount];
  config->ifcount++;

  port = OAM_PORT;
  if (!init_oam_interface(&config->ifaces[config->ifcount], "OAM", NULL, "0.0.0.0", port, ver, cmd_iface)) {
      printf("failed to create oam interface");
  }
  config->ifcount++;
  */
  return true;
}
