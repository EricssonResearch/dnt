// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_IF_UDP_IN_H
#define DNT_IF_UDP_IN_H

struct Interface;

struct Interface *new_udp_in_interface(const char *name, const char *ifname,
        unsigned port, unsigned ipversion, const char *senders);

#endif // DNT_IF_UDP_IN_H
