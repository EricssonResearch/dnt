// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_IF_UDP_OUT_H
#define DNT_IF_UDP_OUT_H

#include <stdbool.h>

struct Interface;

struct Interface *new_udp_out_interface(const char *name, const char *ifname,
        unsigned src_port, const char *dst_ip, unsigned dst_port, unsigned priority);

// set a new destination
// @returns true on success
bool udp_out_set_dst(struct Interface *iface, const char *dst_ip, unsigned dst_port);

#endif // DNT_IF_UDP_OUT_H
