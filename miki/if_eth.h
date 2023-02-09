
#ifndef R2_IF_ETH_H
#define R2_IF_ETH_H

#include "interface.h"

// @returns true if successful
bool init_eth_interface(struct Interface *iface, const char *name, const char *ifname);

#endif // R2_IF_ETH_H
