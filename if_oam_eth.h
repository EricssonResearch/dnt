// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_OAM_ETH_H
#define R2_IF_OAM_ETH_H

struct Interface;

struct Interface *new_oam_eth_interface(const char *name, const char *ifname);

char *oam_eth_if_get_mac(const struct Interface *iface);

unsigned oam_eth_if_get_vlan(const struct Interface *iface);

#endif // R2_IF_OAM_ETH_H
