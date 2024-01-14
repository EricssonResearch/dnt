// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_INTERFACE_H
#define R2_CONF_INTERFACE_H

struct HashMap;
struct IniSection;
struct Interface;
struct ConfStream;

struct ConfStreamList {
    struct ConfStream *stream;
    char *stream_name;
    struct ConfStreamList *next;
};

// returns a hash of the interfaces
// the interfaces are just created, but not opened
// returns NULL on error
struct HashMap *parse_interfaces(const struct IniSection *interfaces_section);

// returns a hash of ConfStreamList keyed by interface name
// returns NULL on error
struct HashMap *parse_interface_streams(const struct IniSection *interfaces_section,
        const struct HashMap *ifaces, const struct HashMap *streams);

#endif // R2_CONF_INTERFACE_H
