// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_H
#define R2_IF_OAM_H

struct Interface;

#define OAM_PORT      6634

struct Interface *new_oam_interface(const char *name,
        const char *oam_ip, unsigned port);

const char *oamif_get_ip(const struct Interface *iface);

unsigned oamif_get_port(const struct Interface *iface);

#endif // R2_IF_OAM_H
