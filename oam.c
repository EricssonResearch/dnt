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


#define OAM_PORT      6634
#define OAM_CMD_PORT  8000

unsigned cmd_id = 1000;

int oam_ping(unsigned id, char *stream, char *mep_start, char *mep_stop, int level){
    printf("OAM ping id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);
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


// Initialize OAM functionality
bool init_oam(struct R2d2Config *config){
    printf("Init OAM fuctionality.\n");

    unsigned port = OAM_CMD_PORT;
    unsigned ver = 4;
    if (!init_oam_cmd_interface(&config->ifaces[config->ifcount], "OAM-CMD", NULL, port, ver)) {
        printf("failed to create oam interface");
    }
    struct Interface *cmd_iface = &config->ifaces[config->ifcount];
    config->ifcount++;


    port = OAM_PORT;
    if (!init_oam_interface(&config->ifaces[config->ifcount], "OAM", NULL, port, ver, cmd_iface)) {
        printf("failed to create oam interface");
    }
    config->ifcount++;

    return true;
}
