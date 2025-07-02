// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_CORE_H
#define R2_OAM_CORE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "hashmap.h"

#include <stdbool.h>

struct Interface *get_oam_interface(const char *ifname);

struct Interface *get_default_oam_ip_interface(void);

struct Interface *get_default_oam_eth_interface(void);

int foreach_oam_ifaces(hashmap_cb *cb, void *userdata);

bool have_default_ip_iface(void);

bool have_default_eth_iface(void);

unsigned short get_default_node_id(void);

#endif // R2_OAM_CORE_H
