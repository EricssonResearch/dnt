// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_ETH_H
#define R2_IF_OAM_ETH_H

struct Interface;

#define OAM_ETHTYPE      0x8902

struct Interface *new_oam_eth_interface(const char *name, const char *ifname);

// @returns a number in host byte order
unsigned short oam_eth_if_get_uid(const struct Interface *iface);

unsigned oam_eth_if_get_vlan(const struct Interface *iface);

#endif // R2_IF_OAM_ETH_H
