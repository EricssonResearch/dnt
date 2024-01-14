// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_CMD_H
#define R2_IF_OAM_CMD_H


struct Interface;

#define OAM_CMD_PORT  8000

struct Interface *new_oam_cmd_interface(const char *name, const char *ifname,
        const char *oam_cmd_ip, unsigned port);

#endif // R2_IF_OAM_CMD_H
