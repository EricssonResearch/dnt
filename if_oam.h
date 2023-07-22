// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_H
#define R2_IF_OAM_H

#include <stdbool.h>

struct Interface;

#define OAM_PORT      6634

bool init_oam_interface(struct Interface *iface, const char *name,
        const char *oam_ip, unsigned port, unsigned ipversion);

const char *oam_get_ip(const struct Interface *iface);
unsigned oam_get_port(const struct Interface *iface);
unsigned short oam_get_uid(const struct Interface *iface);

#endif // R2_IF_OAM_H
