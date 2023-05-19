// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_UDP_IN_H
#define R2_IF_UDP_IN_H

#include <stdbool.h>

struct Interface;

bool init_udp_in_interface(struct Interface *iface, const char *name, const char *ifname,
        unsigned port, unsigned ipversion);

#endif // R2_IF_UDP_IN_H
