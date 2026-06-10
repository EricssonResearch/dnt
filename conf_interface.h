// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_CONF_INTERFACE_H
#define DNT_CONF_INTERFACE_H

#include "conf_streams.h"
#include "inifile.h"

#include <stdbool.h>

struct ConfStreamList {
    struct ConfStream *stream;
    char *stream_name;
    struct ConfStreamList *next;
};

// parses the interface definitions into @ifaces
// the interfaces are just created, but not opened
// @returns false on error
bool parse_interfaces(struct HashMap *ifaces, const struct IniSection *interfaces_section);

// parses the list of streams received by the interfaces
// @returns false on error
bool parse_interface_streams(struct HashMap *iface_streams, const struct IniSection *interfaces_section,
        const struct HashMap *ifaces, const struct HashMap *streams);

#endif // DNT_CONF_INTERFACE_H
