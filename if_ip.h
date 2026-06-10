// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_IF_IP_H
#define DNT_IF_IP_H

struct Interface;

struct Interface *new_ip_interface(const char *name, const char *ifname);

#endif // DNT_IF_IP_H
