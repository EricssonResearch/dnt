// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONFIGFILE_H
#define R2_CONFIGFILE_H

#include <stdbool.h>

struct Interface;
struct HashMap;

// holds all the data we read from the config file
struct R2d2Config {
    struct HashMap *ifaces; // name -> struct Interface
    struct HashMap *objects; // name -> struct PipelineObject
    struct HashMap *streams; // name -> struct ConfStream
    struct HashMap *iface_streams; // ifname -> ConfStreamList
    struct HashMap *oam;     // name -> command string
};

struct R2d2Config *read_config(const char *filename);

// always returns NULL
struct R2d2Config *delete_config(struct R2d2Config *config);

//TODO dynconf: selective add-remove how?
bool config_add_streams_to_interfaces(struct R2d2Config *config);


#endif // R2_CONFIGFILE_H
