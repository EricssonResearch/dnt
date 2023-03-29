
#include "pipeline.h"
#include "action.h"
#include "interface.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

struct Pipeline {
    struct Action *actions; // array of actions
    unsigned action_count;
    unsigned reference_count;
};

// add reference to the outgoing interfaces so they know they are in use
static void ref_send_interfaces(struct Pipeline *pipe)
{
    for (unsigned i=0; i<pipe->action_count; i++) {
        if (pipe->actions[i].type == ACT_SEND) {
            iface_ref(action_send_get_iface(pipe->actions+i));
        }
    }
}

// release the outgoing interfaces
static void unref_send_interfaces(struct Pipeline *pipe)
{
    for (unsigned i=0; i<pipe->action_count; i++) {
        if (pipe->actions[i].type == ACT_SEND) {
            iface_unref(action_send_get_iface(pipe->actions+i));
        }
    }
}

struct Pipeline *new_pipeline(struct Action *actions, unsigned action_count)
{
    struct Pipeline *ret = calloc_struct(Pipeline);
    ret->actions = actions;
    ret->action_count = action_count;
    ref_send_interfaces(ret);
    return ret;
}

void pipeline_ref(struct Pipeline *pipe)
{
    pipe->reference_count++;
}

void pipeline_unref(struct Pipeline *pipe)
{
    if (pipe->reference_count > 0)
        pipe->reference_count--;

    if (pipe->reference_count == 0) {
        unref_send_interfaces(pipe);
        for (unsigned i=0; i<pipe->action_count; i++) {
            delete_action(pipe->actions+i);
        }
        free(pipe->actions);
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

void pipe_iterator_run(struct PipelineIterator *pi)
{
    printf("pipe_iterator_run, action count %u\n", pi->pipe->action_count);
    while (!iterator_done(pi)) {
        struct Action *a = &pi->pipe->actions[pi->pos];
        printf("  action type %d %s '%s'\n", a->type, action_name_from_type(a->type), a->text);
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
