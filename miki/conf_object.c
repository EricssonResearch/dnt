
#include "conf_object.h"
#include "conf_utils.h"
#include "inifile.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct ObjectInfo {
    enum ConfObjectType type;
    union {
        struct {
            bool use_reset_flag;
            bool use_init_flag;
            unsigned init_seq;
        } gen;
        struct {
            bool use_reset_flag;
            bool use_init_flag;
            unsigned history_length;
            unsigned reset_msec;
            unsigned latent_error_paths;
        } rec;
        struct {
            unsigned max_delay; //TODO ms?
            unsigned take_any_time; //TODO ms?
            unsigned buffer_size;
        } pof;
    } p;
};

static bool token_cb(char *str, void *userdata)
{
    struct ObjectInfo *info = userdata;

    if (info->type) {
        char *key, *val;
        if (parse_assignment(str, &key, &val)) {
            switch (info->type) {
                case CO_SEQGEN:
                    if (strcmp(key, "use_reset_flag") == 0) {
                        int reset = read_boolean(val);
                        if (reset < 0) {
                            //TODO throw exception: invalid reset flag
                        }
                        info->p.gen.use_reset_flag = reset;
                    } else if (strcmp(key, "use_init_flag") == 0) {
                        int init = read_boolean(val);
                        if (init < 0) {
                            //TODO throw exception: invalid init flag
                        }
                        info->p.gen.use_init_flag = init;
                    } else if (strcmp(key, "init_seq") == 0) {
                        unsigned seq;
                        if (sscanf(val, "%i", &seq) != 1) {
                            //TODO throw exception: invalid init seq
                        }
                        info->p.gen.init_seq = seq;
                    } else {
                        //TODO throw exception: invalid parameter for gen
                    }
                    break;
                case CO_SEQREC:
                    //TODO
                    break;
                case CO_POF:
                    //TODO
                    break;
            }
        } else {
            //TODO throw exception: invalid object parameter format
        }
    } else {
        if (strcmp(str, "gen") == 0) {
            info->type = CO_SEQGEN;
        } else if (strcmp(str, "rec") == 0) {
            info->type = CO_SEQREC;
        } else if (strcmp(str, "pof") == 0) {
            info->type = CO_POF;
        } else {
            //TODO throw exception: invalid object type
        }
    }

    return true;
}

static void object_cb(const char *key, void *value, void *userdata)
{
    char *desc = value;
    struct HashMap *ret = userdata;

    struct ObjectInfo *info = calloc_struct(ObjectInfo);

    foreach_tokens(desc, token_cb, info);

    struct ConfObject *obj = calloc_struct(ConfObject);
    obj->type = info->type;

    switch (info->type) {
        case CO_SEQGEN:
            obj->object = new_seq_gen(info->p.gen.use_reset_flag,
                    info->p.gen.use_init_flag,
                    info->p.gen.init_seq);
            break;
        case CO_SEQREC:
            obj->object = new_seq_rec(info->p.rec.use_reset_flag,
                    info->p.rec.use_init_flag,
                    info->p.rec.history_length,
                    info->p.rec.reset_msec,
                    info->p.rec.latent_error_paths);
            break;
        case CO_POF:
            //TODO new_pof()
            break;

    }
    hashmap_insert(ret, strdup(key), obj);
}

static void delete_cb(const char *key, void *value, void *userdata)
{
    free((char*)key);
    struct ConfObject *obj = value;
    (void)userdata;
    switch (obj->type) {
        case CO_SEQGEN:
            delete_seq_gen(obj->object);
            break;
        case CO_SEQREC:
            delete_seq_rec(obj->object);
            break;
        case CO_POF:
            //TODO delete_pof()
            break;
    }
    free(obj);
}

struct HashMap *process_objects(struct IniSection *object_section)
{
    struct HashMap *ret = new_hashmap(13, delete_cb, NULL);

    hashmap_foreach(object_section->contents, object_cb, ret);

    return ret;
}
