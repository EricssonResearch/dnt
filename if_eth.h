// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_IF_ETH_H
#define DNT_IF_ETH_H

struct Interface;

// @returns true if successful
struct Interface *new_eth_interface(const char *name, const char *ifname);

#endif // DNT_IF_ETH_H
