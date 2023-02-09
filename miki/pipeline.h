
#ifndef R2_PIPELINE_H
#define R2_PIPELINE_H

#include <stdbool.h>

struct Action;
struct Packet;

struct Pipeline {
    struct Action *actions; // array of actions
    unsigned action_count;
    unsigned iterator_count; // count live iterators for debugging
};

struct PipelineIterator {
    struct Packet *packet;
    struct Pipeline *pipe;
    unsigned pos; // index of current action
};

struct Pipeline *new_pipeline(struct Action *actions, unsigned action_count);

struct Pipeline *delete_pipeline(struct Pipeline *pipe);

// the iterator will own the packet
struct PipelineIterator *new_pipe_iterator(struct Pipeline *pipe, struct Packet *p);

// process the pipeline
// the iterator will delete itself when it's done
void pipe_iterator_run(struct PipelineIterator *pi);


#endif // R2_PIPELINE_H
