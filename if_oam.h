// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_H
#define R2_IF_OAM_H

#include <stdbool.h>

struct Interface;

bool init_oam_interface(struct Interface *iface, const char *name, const char *ifname,
        unsigned port, unsigned ipversion, struct Interface *cmd_iface);

#endif // R2_IF_OAM_H
