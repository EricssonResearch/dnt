// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include "interface.h"
#include "pipeline.h"
#include "hashmap.h"

#define SEQ_IS_OAM(seq)     \
    (ntohl(seq) & 0x10000000u)

// public interface of the OAM module

struct PipelineObject;
struct MepStart;
// state object for a point that receives OAM packets
struct OamEndPoint {
    char *name;
    char *stream;
    int level;
    bool stop; // false: MIP, true: MEP-Stop
    struct MepStart *mep;
};

bool init_oam(void);
void finish_oam(void);

// receive on the return interface
void oam_recv_reply(const char *msg);

// receive on the action pipeline
void oam_recv_request(struct OamEndPoint *oam, struct PipelineIterator *pi);

// receive connection on the command (telnet) interface
void oam_start_command_connection(int fd, const char *remote_ip, unsigned short remote_port);

// print an alert message to all active command sessions (like wall(1) does)
void oam_cli_alert(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((nonnull(1)));

bool oam_create_mep_start(const char *stream_name, const char *mep_name, int level,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx);

// create a structure that represents an OAM request receiver point
// used by MIP and MEP-STOP actions
struct OamEndPoint *oam_create_endpoint(const char *name, const char *stream, int level, bool stop);

// always returns NULL
struct OamEndPoint *oam_delete_endpoint(struct OamEndPoint *end);

// the name of the stream can change within an action pipeline (by jump, replicate, eliminate)
// @parse_actions_line uses this to report the names seen in the pipeline
// this is needed to correctly associate monitoring points with streams
// only the keys of the hash are processed
void oam_stream_names_in_pipeline(struct HashMap *names);

bool set_oam_cmd_if(struct Interface *iface);
void add_oam_if(struct Interface *iface);

bool oam_start_background_ping(const char *name, const char *command);

void oam_count_packet(struct OamEndPoint *oam, struct Packet *p);

#endif // R2_OAM_H
