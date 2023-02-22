
#ifndef R2_CONF_STREAMS_H
#define R2_CONF_STREAMS_H

struct ConfAction;
struct ConfHeader;
struct IniSection;
struct Interface;
struct HashMap;

struct ConfStream {
    struct ConfAction *actions;
    struct ConfHeader *packet;
    struct Interface *recv_iface;
};

// @returns hash of ConfStream keyed by stream name
struct HashMap *parse_streams(struct IniSection *streams_section,
        struct Interface *ifaces, unsigned ifcount,
        struct HashMap *objects);

#endif // R2_CONF_STREAMS_H
