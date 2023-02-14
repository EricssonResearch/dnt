
#include "pipeline.h"
#include "action.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

struct Pipeline *new_pipeline(struct Action *actions, unsigned action_count)
{
    struct Pipeline *ret = calloc_struct(Pipeline);
    ret->actions = actions;
    ret->action_count = action_count;
    return ret;
}

void pipeline_ref(struct Pipeline *pipe)
{
    pipe->reference_count++;
}

void pipeline_unref(struct Pipeline *pipe)
{
    //TODO we also need to refcount the interfaces
    //      pipeline should hold a ref to the interfaces it sends on
    //      iface doesn't destroy itself while we may want to send packet on it
    //      when main closes the interface it stops receiving but still allows sending
    //      this way we can flush the packets still in the system
    //      DynConf: it creates a new iface array, the old one goes away when no longer needed
    pipe->reference_count--;

    if (pipe->reference_count == 0) {
        for (unsigned i=0; i<pipe->action_count; i++) {
            delete_action(pipe->actions+i);
        }
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
    return pi->pos < pi->pipe->action_count;
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
    while (!iterator_done(pi)) {
        struct Action *a = &pi->pipe->actions[pi->pos];
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
