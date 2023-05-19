// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_INTERFACE_H
#define R2_CONF_INTERFACE_H

//#include <stdbool.h>

struct HashMap;
struct IniSection;
struct Interface;
struct ConfStream;

struct ConfStreamList {
    struct ConfStream *stream;
    char *stream_name;
    struct ConfStreamList *next;
};

// returns an array of interfaces and the length of the array
// the interfaces are just created, but not opened
// returns NULL on error
struct Interface *parse_interfaces(struct IniSection *interfaces_section, unsigned *iface_count);

// returns a hash of ConfStreamList keyed by interface name
// returns NULL on error
struct HashMap *parse_interface_streams(struct IniSection *interfaces_section,
        struct Interface *ifaces, unsigned iface_count, struct HashMap *streams);

#endif // R2_CONF_INTERFACE_H
