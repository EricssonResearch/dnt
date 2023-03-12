
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
struct HashMap *parse_streams(struct IniSection *streams_section,
        struct Interface *ifaces, unsigned ifcount,
        struct HashMap *objects);

struct ConfStream *delete_confstream(struct ConfStream *stream);

void stream_add_to_iface(struct ConfStream *stream, struct Interface *recv_iface);

#endif // R2_CONF_STREAMS_H
