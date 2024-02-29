// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_CORE_H
#define R2_OAM_CORE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdio.h>

struct MepStart {
    char *name;
    char *mep_name;
    char *stream_name;
    struct Pipeline *pipe;
    int pipe_pos_idx;
    int level;
};

struct Interface *get_oam_interface(const char *ifname);

void list_oam_ifaces(FILE *cmd_w);

bool have_default_iface(void);

unsigned short get_node_id(void);

struct MepStart *find_mep_start(const char *name);

int foreach_mep_start(hashmap_cb *cb, void *userdata);

#endif // R2_OAM_CORE_H
