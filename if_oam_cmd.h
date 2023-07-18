// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_CMD_H
#define R2_IF_OAM_CMD_H

#include <stdbool.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */

extern unsigned cmd_id;

struct OamCmdIfData {
    int oam_cmd_fd;
    pthread_t oam_tid;
    unsigned port;
    int family;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } srcip;
};

struct Interface;

#define OAM_CMD_PORT  8000

bool init_oam_cmd_interface(struct Interface *iface, const char *name, const char *ifname,
        const char *oam_cmd_ip, unsigned port, unsigned ipversion);

#endif // R2_IF_OAM_CMD_H
