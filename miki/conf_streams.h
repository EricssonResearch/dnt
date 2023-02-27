
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
    struct Interface *recv_iface;
};

// @returns hash of ConfStream keyed by stream name
struct HashMap *parse_streams(struct IniSection *streams_section,
        struct Interface *ifaces, unsigned ifcount,
        struct HashMap *objects);

void stream_add_to_iface(const char *name, struct ConfStream *stream);

#endif // R2_CONF_STREAMS_H
