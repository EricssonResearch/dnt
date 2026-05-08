// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_STREAMS_H
#define R2_CONF_STREAMS_H

#include "inifile.h"
#include "headerdesc.h"

#include <stdbool.h>

struct ConfStream {
    struct ConfAction *actions;
    struct HeaderDescriptor *headers;
};

// parses the stream definitions into @streams
// @ifaces and @objects are the new ones added in the transaction
// @returns false on error
bool parse_streams(struct HashMap *streams, const struct IniSection *streams_section,
        const struct HashMap *ifaces, const struct HashMap *objects);

struct ConfStream *delete_confstream(struct ConfStream *stream);

#endif // R2_CONF_STREAMS_H
