
#ifndef R2_PIPELINE_H
#define R2_PIPELINE_H

#include <stdbool.h>

struct Action;
struct Packet;

struct Pipeline {
    struct Action *actions; // array of actions
    unsigned action_count;
    unsigned reference_count;
};

struct PipelineIterator {
    struct Packet *packet;
    struct Pipeline *pipe;
    unsigned pos; // index of current action
};

// creates a new pipeline
// doesn't automatically reference it!
struct Pipeline *new_pipeline(struct Action *actions, unsigned action_count);

// add a reference to the pipeline
void pipeline_ref(struct Pipeline *pipe);

// remove a reference from the pipeline
// when all references are removed the pipeline deletes itself
void pipeline_unref(struct Pipeline *pipe);

// the iterator will own the packet
struct PipelineIterator *new_pipe_iterator(struct Pipeline *pipe, struct Packet *p);

// process the pipeline
// the iterator will delete itself when it's done
void pipe_iterator_run(struct PipelineIterator *pi);


#endif // R2_PIPELINE_H
