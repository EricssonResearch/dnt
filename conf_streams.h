// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_STREAMS_H
#define R2_CONF_STREAMS_H

struct ConfAction;
struct IniSection;
struct Interface;
struct HashMap;
struct HeaderDescriptor;

struct ConfStream {
    struct ConfAction *actions;
    struct HeaderDescriptor *packet;
};

// @returns hash of ConfStream keyed by stream name
struct HashMap *parse_streams(const struct IniSection *streams_section,
        const struct HashMap *ifaces, const struct HashMap *objects);

struct ConfStream *delete_confstream(struct ConfStream *stream);

#endif // R2_CONF_STREAMS_H
