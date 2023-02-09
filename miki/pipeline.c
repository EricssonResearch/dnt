
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

struct Pipeline *delete_pipeline(struct Pipeline *pipe)
{
    if (!pipe) return NULL;

    if (pipe->iterator_count > 0) {
        //TODO error
    }

    for (unsigned i=0; i<pipe->action_count; i++) {
        delete_action(pipe->actions+i);
    }
    free(pipe);
    return NULL;
}


struct PipelineIterator *new_pipe_iterator(struct Pipeline *pipe, struct Packet *packet)
{
    struct PipelineIterator *ret = calloc_struct(PipelineIterator);
    ret->packet = packet;
    ret->pipe = pipe;
    pipe->iterator_count++;
    return ret;
}


static bool iterator_done(struct PipelineIterator *pi)
{
    return pi->pos < pi->pipe->action_count;
}

static void delete_iterator(struct PipelineIterator *pi)
{
    if (!pi) return;
    if (pi->pipe->iterator_count == 0) {
        fprintf(stderr, "pipe iterator count mismatch!\n");
    } else {
        pi->pipe->iterator_count--;
    }
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
