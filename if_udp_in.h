// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_UDP_IN_H
#define R2_IF_UDP_IN_H

struct Interface;

struct Interface *new_udp_in_interface(const char *name, const char *ifname,
        unsigned port, unsigned ipversion, const char *senders);

#endif // R2_IF_UDP_IN_H
