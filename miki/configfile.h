
#ifndef R2_CONFIGFILE_H
#define R2_CONFIGFILE_H

struct Interface;
struct HashMap;

struct R2d2Config {
    struct Interface *ifaces;
    unsigned ifcount;
    struct HashMap *objects; // values are struct ConfObject
    struct HashMap *streams; // values are struct ConfStream
};

struct R2d2Config *read_config(const char *filename);

struct R2d2Config *delete_config(struct R2d2Config *config);

//TODO dynconf: selective add-remove how?
void config_add_streams_to_interfaces(struct R2d2Config *config);


#endif // R2_CONFIGFILE_H
