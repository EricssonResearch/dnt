// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include "interface.h"
#include "pipeline.h"

// state object for a point that receives OAM packets
struct OamEndPoint {
    char *name;
    char *stream;
    int level;
    struct ConfObject *target; // PRF, PEF, POF, etc.
    bool stop;
};

bool init_oam(struct HashMap *config_oam);
void finish_oam(void);

int oam_recv_reply(const char *msg);

// @returns true if the packet should be forwarded
bool oam_recv_request(struct OamEndPoint *oam, struct Packet *p);

void oam_start_command_connection(int fd);

// If there is an active OAM CLI session this function
// can print messages into it (e.g. alerting the operator
// from imporant warnings or failures)
void oam_cli_alert(const char *fmt, ...);

int oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx);
void oam_set_pipeline_for_mep_start(const char *stream_name, struct Pipeline *pipe);

struct OamEndPoint *oam_create_endpoint(const char *name, const char *stream, int level, struct ConfObject *target, bool stop);

bool set_oam_cmd_if(struct Interface *iface);
void add_oam_if(struct Interface *iface);

#endif // R2_OAM_H
