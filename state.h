// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_STATE_H
#define DNT_STATE_H

#include "hashmap.h"
#include "interface.h"
#include "object.h"

#include <stdbool.h>

// holds all the data we change in one step
struct StateTransaction {
    char *name;

    // things to add
    struct HashMap *ifaces; // name -> struct Interface
    struct HashMap *objects; // name -> struct PipelineObject
    struct HashMap *streams; // name -> struct ConfStream
    struct HashMap *iface_streams; // ifname -> ConfStreamList
    struct HashMap *oam;     // name -> command string

    // things to remove
    struct HashMap *del_ifaces; // name -> NULL
    struct HashMap *del_streams; //TODO tuples of {iface name, stream name}
};

// creates a new empty transaction named @name
struct StateTransaction *new_transaction(const char *name);

// always returns NULL
struct StateTransaction *delete_transaction(struct StateTransaction *tr);


// the state of DNT is a singleton object

struct Interface *state_get_interface(const char *ifname);

struct PipelineObject *state_get_object(const char *objname);

// returning false from the callback aborts the foreach
typedef int state_foreach_if_cb(struct Interface *iface, void *userdata);
int state_foreach_interfaces(state_foreach_if_cb *cb, void *userdata);

// returning false from the callback aborts the foreach
typedef int state_foreach_obj_cb(struct PipelineObject *obj, void *userdata);
int state_foreach_objects(state_foreach_obj_cb *cb, void *userdata);

// make the changes described in in @tr to the active configuration
// @returns true if the commit was successful
bool state_commit_transaction(struct StateTransaction *tr);


// the name of the stream can change throughout an action pipeline (by jump, replicate, eliminate)
//  with this we know what names are associated with the same stream
//  OAM needs this to correctly associate maintenance points with streams
// @parse_actions_line calls this to report the stream names seen in an action pipeline
//  TODO do this reporting in the pipeline assembler
// only the keys of the hash are processed
void stream_names_in_pipeline(struct HashMap *names);

// @returns true if @s1 and @s2 are part of the same compound stream
// the stream name can change throughout an action pipeline (by jump, replicate, eliminate)
// this function uses the data submitted via @stream_names_in_pipeline
bool same_compound_stream(const char *s1, const char *s2);


#endif // DNT_STATE_H
