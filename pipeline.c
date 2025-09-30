// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "pipeline.h"
#include "action.h"
#include "interface.h"
#include "json.h"
#include "log.h"
#include "notification.h"
#include "utils.h"

#include <stdlib.h>

DEFAULT_LOGGING_MODULE(PIPELINE, WARNING);
LOGGING_MODULE(PACKETTRACE, WARNING);

void pipeline_ref_send_interfaces(struct Pipeline *pipe)
{
    for (unsigned i=0; i<pipe->action_count; i++) {
        if (pipe->actions[i].type == ACT_SEND) {
            iface_add_sender(action_send_get_iface(pipe->actions+i));
        } else if (pipe->actions[i].type == ACT_REPL) {
            struct PipelineList *pl = action_repl_get_piplinelist(pipe->actions+i);
            while (pl) {
                pipeline_ref_send_interfaces(pl->pipe);
                pl = pl->next;
            }
        }
    }
}

// release the outgoing interfaces
static void unref_send_interfaces(struct Pipeline *pipe)
{
    for (unsigned i=0; i<pipe->action_count; i++) {
        if (pipe->actions[i].type == ACT_SEND) {
            iface_del_sender(action_send_get_iface(pipe->actions+i));
        }
    }
}

void pipeline_ref(struct Pipeline *pipe)
{
    __atomic_add_fetch(&pipe->reference_count, 1, __ATOMIC_RELAXED);
}

void pipeline_unref(struct Pipeline *pipe)
{
    int refcount = __atomic_sub_fetch(&pipe->reference_count, 1, __ATOMIC_RELAXED);

    if (refcount == 0) {
        unref_send_interfaces(pipe);
        for (unsigned i=0; i<pipe->action_count; i++) {
            delete_action(pipe->actions+i);
        }
        free(pipe->actions);
        free(pipe->name);
        free(pipe);
    }
}


struct PipelineIterator *new_pipe_iterator(struct Pipeline *pipe, struct Packet *packet)
{
    struct PipelineIterator *ret = calloc_struct(PipelineIterator);
    ret->packet = packet;
    ret->pipe = pipe;
    pipeline_ref(pipe);
    return ret;
}

static bool iterator_done(struct PipelineIterator *pi)
{
    return pi->pos >= pi->pipe->action_count;
}

static void delete_iterator(struct PipelineIterator *pi)
{
    if (!pi) return;
    pipeline_unref(pi->pipe);
    delete_packet(pi->packet);
    free(pi);
}

bool pipe_set_mask(struct Pipeline *pipe, bool new_mask)
{
    if (pipe->mask != new_mask) {
        pipe->mask = new_mask;

        struct JsonValue *noti = json_object();
        json_object_insert(noti, "source_pipeline", json_string(pipe->name));
        json_object_insert(noti, "status", json_string(new_mask ? "masked" : "unmasked"));
        notification_push_event("mask", NOTIF_INFO, noti);

        return true;
    }
    return false;
}

void pipe_iterator_run(struct PipelineIterator *pi)
{
    log_packet("pipe_iterator_run %s, action count %u", pi->pipe->name, pi->pipe->action_count);
    while (!iterator_done(pi)) {
        struct Action *a = &pi->pipe->actions[pi->pos];
        log_packet("  action type %d %s '%s'", a->type, action_name_from_type(a->type), a->text);
        PACKET_LOGCAT(pi->packet, "%s ", action_name_from_type(a->type));
        enum ActionResult res = a->execute(a, pi);
        switch (res) {
            case ACR_CONTINUE:
                pi->pos++;
                break;
            case ACR_DONE:
                delete_iterator(pi);
                return;
            case ACR_HOLD:
                return;
        }
    }
    delete_iterator(pi);
}

void pipe_iteraror_cancel(struct PipelineIterator *pi)
{
    delete_iterator(pi);
}

struct JsonValue *pipe_get_state(const struct Pipeline *pipe)
{
    struct JsonValue *state = json_object();
    json_object_insert(state, "name", json_string(pipe->name));
    json_object_insert(state, "mask_state", json_string(pipe->mask ? "masked" : "unmasked"));
    json_object_insert(state, "action_count", json_number((double) pipe->action_count));
    return state;
}
