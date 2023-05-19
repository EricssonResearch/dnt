// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_ETH_H
#define R2_IF_ETH_H

#include <stdbool.h>

struct Interface;

// @returns true if successful
bool init_eth_interface(struct Interface *iface, const char *name, const char *ifname);

#endif // R2_IF_ETH_H
