// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include "interface.h"
#include "object.h"
#include "pipeline.h"
#include "hashmap.h"

#define OAM_CFM_REQUEST_OPCODE 128
#define OAM_CFM_RESPONSE_OPCODE 129

#define SEQ_IS_OAM(seq)     \
    (ntohl(seq) & 0x10000000u)

// public interface of the OAM module

enum OAM_MP_Type { OAM_Start, OAM_Stop, OAM_Intermediate };

enum OAM_MP_Encap { OAM_PW, OAM_TSN, OAM_SRv6 };

// state object for a pipeline action that sends/receives OAM packets
struct OAM_MaintenancePoint;

// initialize the OAM module
// @returns true on success
bool init_oam(void);

// stops the OAM module and cleans up its resources
void finish_oam(void);

// receive on the action pipeline
// keeps hold of @pi, the action should return ACR_HOLD
void oam_receive_inband(struct OAM_MaintenancePoint *mp, struct PipelineIterator *pi);

// receive on a return interface
// makes a copy of @message
void oam_receive_outofband(struct Interface *iface, const char *message);

// receive connection on the command (telnet) interface
void oam_start_command_connection(int fd, const char *remote_ip, unsigned short remote_port);

// print an alert message to all active command sessions (like wall(1) does)
void oam_cli_alert(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((nonnull(1)));

// creates a maintenance point or adds reference to an existing one
// @returns pointer to the MP
// MPs are uniquely identified by @mp_name
// when multiple actions refer to the same MP the @stream_name, @type and @level must be the same
// MP can be bound to a pipeline object @obj to monitor its state
// if MP is an injector point (OAM_Start, OAM_Intermediate), @pipe and @idx define the injection point
// @protostack determines the encapsulation of the MP
struct OAM_MaintenancePoint *oam_new_maintenance_point(const char *stream_name, const char *mp_name,
        enum OAM_MP_Type type, unsigned level,
        const enum ProtocolID *protostack,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx);

// removes a reference from @mp
// @mp is deleted when its reference count reaches zero
void oam_unref_maintenance_point(struct OAM_MaintenancePoint *mp);


// specify the command interface that receives telnet connections
// see @oam_start_command_connection
bool set_oam_cmd_if(struct Interface *iface);

// registers an oam return interface that receives out-of-band replies
// see @oam_recv_reply
void add_oam_if(struct Interface *iface);

// start an infinitely running OAM ping session
// it runs in the background, i.e. it is not associated with a telnet session
bool oam_start_background_ping(const char *name, const char *command);

// decode the DetNet Associated Channel Header (PROTO_ID_OAM)
#define INTERPRET_DACH(header)                                                      \
    struct {                                                                        \
        unsigned char version;                                                      \
        unsigned char seq;                                                          \
        unsigned short channel;                                                     \
        unsigned int nodeid;                                                        \
        unsigned char level;                                                        \
        unsigned char flags;                                                        \
        unsigned char session;                                                      \
    } dach;                                                                         \
    dach.version = (header)[0] & 0xf;                                               \
    dach.seq = (header)[1];                                                         \
    dach.channel = ((header)[2] << 8) + ((header)[3]);                              \
    dach.nodeid = ((header)[4] << 12) + ((header)[5] << 4) + ((header)[6] >> 4);    \
    dach.level = ((header)[6] >> 1) & 0x7;                                          \
    dach.flags = (((header)[6] & 0x1) << 4) + ((header)[7] >> 4);                   \
    dach.session = ((header)[7] & 0xf)

//TODO functions to set the dACH fields


#endif // R2_OAM_H
