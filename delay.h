// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_DELAY_H
#define DNT_DELAY_H

#include "pipeline.h"

// this has to be CLOCK_REALTIME because MessageQueue can't use anything else
#define DELAY_CLOCK CLOCK_REALTIME

// initialize the delay module
// @returns true on success
bool init_delay(void);

// finish with the delay module
void finish_delay(void);

// inserts the iterator @pi into a queue to be sent later
// @pi->packet->delay is the minimum time the packet must spend in the network
//  (this metadata is automatically filled by the `delay` action from its configuration)
// @pi->packet->timestamp should be the TSN timestamp when the packet has entered our network
//  (this metadata MUST be filled by a `readtstamp` action from the appropriate header)
// the queue will release the packet when @delay has elapsed since @timestamp
void delay_insert(struct PipelineIterator *pi);

// registers a delay notification that reports the per-stream counters
bool register_delay_notification(bool add, unsigned period_ms);

// calls @cb for each delay stat entry
// stops iterating if @cb returns 0
int foreach_delay_stat(int (*cb)(const char *pipe, uint64_t packets, uint64_t delay_exceeded, void *userdata), void *userdata);

#endif // DNT_DELAY_H
