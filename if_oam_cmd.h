// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_CMD_H
#define R2_IF_OAM_CMD_H

#include "configfile.h"
#include "hashmap.h"
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */

struct Interface;

#define OAM_CMD_PORT  8000

bool init_oam_cmd_interface(struct Interface *iface, const char *name, const char *ifname,
        const char *oam_cmd_ip, unsigned port, unsigned ipversion);
int oam_cmd_recv_reply(struct Interface *iface, char *msg);

#endif // R2_IF_OAM_CMD_H
