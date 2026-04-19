// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_DELAY_H
#define R2_DELAY_H

#include "pipeline.h"
#include "time_utils.h"
#include "notification.h"

// initialize the delay thread
// @returns true on success
bool init_delay(void);

// finish with the delay thread
void finish_delay(void);

// inserts the iterator @pi into a queue to be sent later
// @pi->packet->delay is the minimum time the packet must spend in the network
//  (this metadata is automatically filled by the `delay` action from its configuration)
// @pi->packet->timestamp should be the TSN timestamp when the packet has entered our network
//  (this metadata MUST be filled by a `readtstamp` action from the appropriate header)
// the queue will release the packet when @delay has elapsed since @timestamp
void delay_insert(struct PipelineIterator *pi);

// registers a delay notification
bool register_delay_notification(bool add, unsigned period_ms);

#endif // R2_DELAY_H
