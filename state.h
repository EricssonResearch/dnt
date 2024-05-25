// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_STATE_H
#define R2_STATE_H

#include "hashmap.h"

#include <stdbool.h>

struct Interface;
struct PipelineObject;

// holds all the data we change in one step
struct StateTransaction {
    char *name;
    //TODO all of these are things to be added, we need a remove list too
    struct HashMap *ifaces; // name -> struct Interface
    struct HashMap *objects; // name -> struct PipelineObject
    struct HashMap *streams; // name -> struct ConfStream
    struct HashMap *iface_streams; // ifname -> ConfStreamList
    struct HashMap *oam;     // name -> command string
};

struct StateTransaction *new_transaction(const char *name);

// always returns NULL
struct StateTransaction *delete_transaction(struct StateTransaction *tr);


// the state of R2DTWO is a singleton object

struct Interface *state_get_interface(const char *ifname);

struct PipelineObject *state_get_object(const char *objname);

int state_foreach_interfaces(hashmap_cb *cb, void *userdata);

int state_foreach_objects(hashmap_cb *cb, void *userdata);

// make the changes described in in @tr to the active configuration
// @returns true if the commit was successful
bool state_commit_transaction(struct StateTransaction *tr);

//TODO better name?
void finish_state(void);

#endif // R2_STATE_H
