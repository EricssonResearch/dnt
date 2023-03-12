
#ifndef R2_CONFIGFILE_H
#define R2_CONFIGFILE_H

#include <stdbool.h>

struct Interface;
struct HashMap;

struct R2d2Config {
    struct Interface *ifaces; //TODO store the interfaces in a hash table
    unsigned ifcount;
    struct HashMap *objects; // name -> struct ConfObject
    struct HashMap *streams; // name -> struct ConfStream
    struct HashMap *iface_streams; // ifname -> ConfStreamList
};

struct R2d2Config *read_config(const char *filename);

// always returns NULL
struct R2d2Config *delete_config(struct R2d2Config *config);

//TODO dynconf: selective add-remove how?
bool config_add_streams_to_interfaces(struct R2d2Config *config);


#endif // R2_CONFIGFILE_H
