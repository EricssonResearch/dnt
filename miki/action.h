
#ifndef R2_ACTION_H
#define R2_ACTION_H

#include <stddef.h>

enum ActionType {
    ACT_ADD = 1,
    ACT_DEL,
    ACT_DELAY,
    ACT_DROP,
    ACT_EDIT,
    ACT_ELIM,
    ACT_POF,
    ACT_REPL,
    ACT_SEND,
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

typedef enum ActionResult action_execute(struct Action *a, struct PipelineIterator *pi);

typedef void action_del(void *action_private);

struct Action {
    enum ActionType type;
    action_execute *execute;
    action_del *del;
    void *action_private;
    char *text; // textual representation (as it was in the config)
};

//TODO move this struct to another header?
struct PipelineList {
    struct Pipeline *pipe;
    const char *text;
    struct PipelineList *next;
};


// this just adds the header, the fields will be set with an edit action
void create_action_add(struct Action *a, unsigned idx, int type, size_t len, const char *text);

void create_action_del(struct Action *a, unsigned idx, const char *text);

//TODO some upper bits of the timestamp are flags!
void create_action_delay(struct Action *a, unsigned delay_ms, struct HeaderField *timestamp, const char *text);

void create_action_drop(struct Action *a, const char *text);

// receives an array of assignments, the action will do them all
// can edit multiple different headers at once
void create_action_edit(struct Action *a, struct HeaderFieldAssign *assigns, unsigned assign_count, const char *text);

//TODO receive a sequence recovery object
void create_action_elim(struct Action *a, struct HeaderField *sequence, const char *text);

//TODO receive a pof object
void create_action_pof(struct Action *a, struct HeaderField *sequence, const char *text);

void create_action_repl(struct Action *a, struct PipelineList *list, const char *text);

void create_action_send(struct Action *a, struct Interface *iface, const char *text);

struct Action *delete_action(struct Action *a);

#endif // R2_ACTION_H
