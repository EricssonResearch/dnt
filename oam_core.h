// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_CORE_H
#define R2_OAM_CORE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <time.h>

struct Packet;

//TODO can we make this private? oam_message.c uses it heavily
struct MepStart {
    char *name;
    char *stream_name;
    struct Pipeline *pipe;
    unsigned packets_passed;
    unsigned octets_passed;
    unsigned oam_packets_passed;
    int pipe_pos_idx;
    int level;
    struct PipelineObject *target; // PRF, PEF, POF, etc.
    struct timespec last_mask_heartbeat; // last mask signal received
    struct Thread *mask_check_worker; // periodically check if path(s) masked
};

struct Interface *get_oam_interface(const char *ifname);

struct Interface *get_default_oam_interface(void);

int foreach_oam_ifaces(hashmap_cb *cb, void *userdata);

bool have_default_iface(void);

unsigned short get_node_id(void);

struct MepStart *find_mep_start(const char *name);

int foreach_mep_start(hashmap_cb *cb, void *userdata);

int print_mep_start(const struct MepStart *start, FILE *cmd_w);

bool mep_start_in_stream(const struct MepStart *start, const char *stream);

void mep_start_wakeup_mask_checker(struct MepStart *start);

void mep_start_count_passed(struct MepStart *start, struct Packet *pkt);

#endif // R2_OAM_CORE_H
