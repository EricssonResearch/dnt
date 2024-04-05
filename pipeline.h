// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_PIPELINE_H
#define R2_PIPELINE_H

#include <stdbool.h>

struct Action;
struct Packet;

struct Pipeline {
    struct Action *actions; // array of actions
    unsigned action_count;
    int reference_count;
    char *name;
};


struct PipelineIterator {
    struct Packet *packet;
    struct Pipeline *pipe;
    unsigned pos; // index of current action
};

// note: create pipeline with assemble_actions()

// add a reference to the pipeline
void pipeline_ref(struct Pipeline *pipe);

// remove a reference from the pipeline
// when all references are removed the pipeline deletes itself
void pipeline_unref(struct Pipeline *pipe);

// the iterator will own the packet
struct PipelineIterator *new_pipe_iterator(struct Pipeline *pipe, struct Packet *p);

// process the pipeline
// the iterator will delete itself and the associated packet when it's done
void pipe_iterator_run(struct PipelineIterator *pi);

// drop the iterator and the associated packet
// only needed in very specific cases
void pipe_iteraror_cancel(struct PipelineIterator *pi);

#endif // R2_PIPELINE_H
