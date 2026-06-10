// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_IF_OAM_CMD_H
#define DNT_IF_OAM_CMD_H


struct Interface;

#define OAM_CMD_PORT  8000

struct Interface *new_oam_cmd_interface(const char *name, const char *ifname,
        const char *oam_cmd_ip, unsigned port);

#endif // DNT_IF_OAM_CMD_H
