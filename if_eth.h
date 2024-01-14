// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_ETH_H
#define R2_IF_ETH_H

struct Interface;

// @returns true if successful
struct Interface *new_eth_interface(const char *name, const char *ifname);

#endif // R2_IF_ETH_H
