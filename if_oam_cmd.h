// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_CMD_H
#define R2_IF_OAM_CMD_H

#include <stdbool.h>
#include <stdio.h>

struct Interface;

#define OAM_CMD_PORT  8000

enum TerminalFormat{
    NONE,
    DUMP,
    JSON,
};

bool init_oam_cmd_interface(struct Interface *iface, const char *name, const char *ifname,
        const char *oam_cmd_ip, unsigned port);
int oam_cmd_recv_reply(struct Interface *iface, const char *msg);
int oam_get_cmd_fd(struct Interface *iface);
FILE *oam_get_cmd_w(struct Interface *iface);
enum TerminalFormat oam_cmd_get_mode(struct Interface *iface);
void oam_cmd_set_mode(struct Interface *iface, enum TerminalFormat mode);

#endif // R2_IF_OAM_CMD_H
