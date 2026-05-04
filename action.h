// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_ACTION_H
#define R2_ACTION_H

#include "header.h"
#include "pipeline.h"
#include "time_utils.h"

enum ActionType {
    ACT_ADD = 1,
    ACT_CHECKSUM,
    ACT_DEL,
    ACT_DELAY,
    ACT_DROP,
    ACT_EDIT,
    ACT_ELIM,
    ACT_FILTEROAM,
    ACT_OAMINJECT,
    ACT_OAMRECEIVE,
    ACT_POF,
    ACT_READSEQ,
    ACT_READTSTAMP,
    ACT_REPL,
    ACT_SEND,
    ACT_SEQGEN,
    ACT_SETLENGTH,
    ACT_TTLCHECK,
    ACT_TTLREDUCE,
    ACT_VERIFY,
    ACT_WRITESEQ,
    ACT_WRITETSTAMP,
};

enum ActionResult {
    ACR_CONTINUE, // can go to next action
    ACR_DONE, // end of processing, free the packet
    ACR_HOLD, // stop processing, keep the packet
};

struct Action;
struct OAM_MP_Address; // we can't include oam.h
struct PipelineObject; // we can't include object.h

typedef enum ActionResult action_execute(struct Action *a, struct PipelineIterator *pi);

typedef void action_del(void *action_private);

// describes one action, we have an array of these in Pipeline
// @type and @text are primarily for debug
// actions perform their task via the @execute function
// actions remember type-specific stuff in @action_private
// the @action_private is deleted with the @del function
struct Action {
    enum ActionType type;
    action_execute *execute;
    action_del *del;
    void *action_private;
    char *text; // textual representation (as it was in the config)
};

// this is the state of the Edit action
// if @read is NULL then it is a constant value
struct EditAssign {
    value_consumer *write;
    void *write_state; // currently this is always a struct HeaderField *
    value_producer *read;
    void *read_state;
    struct Value constant;
    char *text;
    bool owns_read_state;
};

// this is the state of the Replicate action
struct PipelineList {
    struct Pipeline *pipe;
    struct PipelineList *next;
};


const char *action_name_from_type(enum ActionType type);

// this just adds the header, the fields will be set with an edit action
void create_action_add(struct Action *a, unsigned idx, enum ProtocolID type, unsigned len, const char *text);

void create_action_checksum(struct Action *a,
        unsigned hdr_idx, enum ProtocolID hdr_proto, unsigned ip_idx, enum ProtocolID ip_version,
        const char *text);

void create_action_del(struct Action *a, unsigned idx, const char *text);

void create_action_delay(struct Action *a, const struct timespec delay, bool offload, const char *text);

void create_action_drop(struct Action *a, const char *text);

// receives an array of assignments, the action will do them all
// can edit multiple different headers at once
void create_action_edit(struct Action *a, struct EditAssign *assigns, unsigned assign_count, const char *text);

void create_action_elim(struct Action *a, struct PipelineObject *rcvy, const enum ProtocolID *protostack, const char *text);

void create_action_filteroam(struct Action *a, const struct HeaderField *seqfield, const char *text);

// @returns false on error
bool create_action_oam_inject(struct Action *a, const char *name, const char *stream, int level,
                              bool intermediate, struct Pipeline *pipe, unsigned idx,
                              struct OAM_MP_Address *address,
                              const enum ProtocolID *protostack,
                              struct PipelineObject *obj, const char *text);

// @returns false on error
bool create_action_oam_receive(struct Action *a, const char *name, const char *stream, int level,
                               bool intermediate, const enum ProtocolID *protostack,
                               struct PipelineObject *obj, const char *text);

void create_action_pof(struct Action *a, struct PipelineObject *pof, const char *text);

void create_action_readseq(struct Action *a, const struct HeaderField *seqfield, const char *text);

void create_action_readtstamp(struct Action *a, const struct HeaderField *tsfield, const char *text);

void create_action_repl(struct Action *a, struct PipelineList *list, struct PipelineObject *replobj,
        const enum ProtocolID *protostack, const char *text);

void create_action_send(struct Action *a, struct Interface *iface, const char *text);

void create_action_seqgen(struct Action *a, struct PipelineObject *gen, const char *text);

void create_action_setlength(struct Action *a, const struct HeaderField *field,
        unsigned baselen, unsigned payload_idx, const char *text);

void create_action_ttlcheck(struct Action *a, const char *text);

void create_action_ttlreduce(struct Action *a, const struct HeaderField *ttlfield, const char *text);

void create_action_verify(struct Action *a,
        unsigned hdr_idx, enum ProtocolID hdr_proto, unsigned ip_idx, enum ProtocolID ip_version,
        const char *text);

void create_action_writeseq(struct Action *a, const struct HeaderField *seqfield, const char *text);

void create_action_writetstamp(struct Action *a, const struct HeaderField *tsfield, const char *text);


// send action returns its interface
struct Interface *action_send_get_iface(struct Action *a);

// replicate action returns its pipelines
struct PipelineList *action_repl_get_piplinelist(struct Action *a);

// frees the dynamic memory used by the action, but not the action itself (they are in an array)
struct Action *delete_action(struct Action *a);

#endif // R2_ACTION_H
