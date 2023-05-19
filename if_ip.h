// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_IP_H
#define R2_IF_IP_H

#include <stdbool.h>

struct Interface;

// @returns true if successful
bool init_ip_interface(struct Interface *iface, const char *name, const char *ifname);

#endif // R2_IF_IP_H
