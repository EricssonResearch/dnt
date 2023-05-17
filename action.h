
#ifndef R2_ACTION_H
#define R2_ACTION_H

#include "transfer.h"

enum ActionType {
    ACT_ADD = 1,
    ACT_DEL,
    ACT_DELAY,
    ACT_DROP,
    ACT_EDIT,
    ACT_ELIM,
    ACT_POF,
    ACT_READSEQ,
    ACT_READTSTAMP,
    ACT_REPL,
    ACT_SEND,
    ACT_SEQGEN,
    ACT_WRITESEQ,
    ACT_WRITETSTAMP,
};

enum ActionResult {
    ACR_CONTINUE, // can go to next action
    ACR_DONE, // end of processing, free the packet
    ACR_HOLD, // stop processing, keep the packet
};

struct Action;
struct HeaderField;
struct HeaderFieldAssign;
struct Interface;
struct Packet;
struct PipelineIterator;
struct SequenceGenerator;
struct SequenceRecovery;
struct Pof;

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

// this is the state of Replicate
struct PipelineList {
    struct Pipeline *pipe;
    const char *text;
    struct PipelineList *next;
};

// this is the state of Edit
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


const char *action_name_from_type(enum ActionType type);

// this just adds the header, the fields will be set with an edit action
void create_action_add(struct Action *a, unsigned idx, int type, unsigned len, const char *text);

void create_action_del(struct Action *a, unsigned idx, const char *text);

//TODO some upper bits of the timestamp are flags!
void create_action_delay(struct Action *a, unsigned delay_ms, const char *text);

void create_action_drop(struct Action *a, const char *text);

// receives an array of assignments, the action will do them all
// can edit multiple different headers at once
void create_action_edit(struct Action *a, struct EditAssign *assigns, unsigned assign_count, const char *text);

void create_action_elim(struct Action *a, struct SequenceRecovery *rcvy, const char *text);

void create_action_pof(struct Action *a, struct Pof *pof, const char *text);

void create_action_readseq(struct Action *a, const struct HeaderField *seqfield, const char *text);

void create_action_readtstamp(struct Action *a, const struct HeaderField *tsfield, const char *text);

void create_action_repl(struct Action *a, struct PipelineList *list, const char *text);

void create_action_send(struct Action *a, struct Interface *iface, const char *text);

void create_action_seqgen(struct Action *a, struct SequenceGenerator *gen, const char *text);

void create_action_writeseq(struct Action *a, const struct HeaderField *seqfield, const char *text);

void create_action_writetstamp(struct Action *a, const struct HeaderField *tsfield, const char *text);


// send action returns its interface
struct Interface *action_send_get_iface(struct Action *a);

// replicate action returns its pipelines
struct PipelineList *action_repl_get_piplinelist(struct Action *a);

// frees the dynamic memory used by the action, but not the action itself (they are in an array)
struct Action *delete_action(struct Action *a);

#endif // R2_ACTION_H
