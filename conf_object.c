// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_object.h"
#include "conf_utils.h"
#include "inifile.h"
#include "pof.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct ForeachState {
    struct HashMap *objects;
};

struct ObjectInfo {
    const char *name;
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
            enum SequenceRecoveryAlgorithm algo;
        } rec;
        struct {
            unsigned max_delay; //TODO ms?
            unsigned take_any_time; //TODO ms?
            unsigned buffer_size;
        } pof;
    } p;
};

static void set_default_parameters(struct ObjectInfo *info)
{
    switch (info->type) {
        case CO_SEQGEN:
            info->p.gen.use_reset_flag = false;
            info->p.gen.use_init_flag = false;
            info->p.gen.init_seq = 0x8000; // based on the old code
            break;
        case CO_SEQREC:
            info->p.rec.use_reset_flag = false;
            info->p.rec.use_init_flag = false;
            info->p.rec.history_length = 2;
            info->p.rec.reset_msec = 2000;
            info->p.rec.latent_error_paths = 2;
            info->p.rec.algo = RCVY_Vector;
            break;
        case CO_POF:
            info->p.pof.max_delay = 20;
            info->p.pof.take_any_time = 2000;
            info->p.pof.buffer_size = 2; // = info->p.rec.history_length
            break;
        case CO_REPL:
            // nothing to init
            break;
    }
}

static bool token_cb(char *str, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "object %s error: " msg "\n",               \
                info->name, ##__VA_ARGS__);                         \
        return false;                                               \
    } while (0)

    struct ObjectInfo *info = userdata;

    if (info->type) {
        char *key, *val;
        if (parse_assignment(str, &key, &val)) {
            switch (info->type) {
                case CO_SEQGEN:
                    if (strcmp(key, "ResetFlag") == 0) {
                        int reset = read_boolean(val);
                        if (reset < 0) {
                            THROW("invalid reset flag");
                        }
                        info->p.gen.use_reset_flag = reset;
                    } else if (strcmp(key, "InitSeqFlag") == 0) {
                        int init = read_boolean(val);
                        if (init < 0) {
                            THROW("invalid init flag");
                        }
                        info->p.gen.use_init_flag = init;
                    } else if (strcmp(key, "InitSeqStart") == 0) {
                        unsigned seq;
                        char err;
                        if (sscanf(val, "%i%c", &seq, &err) != 1) {
                            THROW("invalid init sequence number '%s'", val);
                        }
                        info->p.gen.init_seq = seq;
                    } else {
                        THROW("invalid parameter '%s' for sequence generator", key);
                    }
                    break;
                case CO_SEQREC:
                    if (strcmp(key, "ResetFlag") == 0) {
                        int reset = read_boolean(val);
                        if (reset < 0) {
                            THROW("invalid reset flag");
                        }
                        info->p.rec.use_reset_flag = reset;
                    } else if (strcmp(key, "InitSeqFlag") == 0) {
                        int init = read_boolean(val);
                        if (init < 0) {
                            THROW("invalid init flag");
                        }
                        info->p.rec.use_init_flag = init;
                    } else if (strcmp(key, "frerSeqRcvyHistoryLength") == 0) {
                        unsigned hlen;
                        char err;
                        if (sscanf(val, "%i%c", &hlen, &err) != 1) {
                            THROW("invalid history length '%s'", val);
                        }
                        info->p.rec.history_length = hlen;
                    } else if (strcmp(key, "frerSeqRcvyResetMSec") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid reset msec '%s'", val);
                        }
                        info->p.rec.reset_msec = msec;
                    } else if (strcmp(key, "frerSeqRcvyLatentErrorPaths") == 0) {
                        unsigned path;
                        char err;
                        if (sscanf(val, "%i%c", &path, &err) != 1) {
                            THROW("invalid latent error paths '%s'", val);
                        }
                        info->p.rec.latent_error_paths = path;
                    } else if (strcmp(key, "frerSeqRcvyAlgorithm") == 0) {
                        if (strcmp(val, "Vector") == 0) {
                            info->p.rec.algo = RCVY_Vector;
                        } else if (strcmp(val, "SeamlessVector") == 0) {
                            info->p.rec.algo = RCVY_SeamlessVector;
                        } else if (strcmp(val, "Match") == 0) {
                            info->p.rec.algo = RCVY_Match;
                        } else {
                            THROW("invalid recovery algorithm '%s'", val);
                        }
                    } else {
                        THROW("invalid parameter '%s' for sequence recovery", key);
                    }
                    break;
                case CO_POF:
                    if (strcmp(key, "TakeAnyTime") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid take any time '%s'", val);
                        }
                        info->p.pof.take_any_time = msec;
                    } else if (strcmp(key, "MaxDelay") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid max delay time '%s'", val);
                        }
                        info->p.pof.max_delay = msec;
                    } else if (strcmp(key, "BufferSize") == 0) {
                        unsigned size;
                        char err;
                        if (sscanf(val, "%i%c", &size, &err) != 1) {
                            THROW("invalid max delay time '%s'", val);
                        }
                        info->p.pof.buffer_size = size;
                    } else {
                        THROW("invalid parameter '%s' for packet ordering function", key);
                    }
                    break;
                case CO_REPL:
                    THROW("invalid parameter '%s' for replication", key);
                    break;
            }
        } else {
            THROW("object parameter '%s' has invalid format", str);
        }
    } else {
        if (strcmp(str, "SeqGen") == 0) {
            info->type = CO_SEQGEN;
        } else if (strcmp(str, "SeqRcvy") == 0) {
            info->type = CO_SEQREC;
        } else if (strcmp(str, "Pof") == 0) {
            info->type = CO_POF;
        } else if (strcmp(str, "Replicate") == 0) {
            info->type = CO_REPL;
        } else {
            THROW("invalid type '%s'", str);
        }
        set_default_parameters(info);
    }

    return true;
#undef THROW
}

static int object_cb(const char *key, void *value, void *userdata)
{
    char *desc = value;
    struct ForeachState *state = userdata;
    struct ObjectInfo info = {0};
    info.name = key;

    if (!foreach_tokens(desc, token_cb, &info)) {
        fprintf(stderr, "error parsing parameters for object '%s'\n", key);
        return 0;
    }

    struct ConfObject *obj = calloc_struct(ConfObject);
    obj->type = info.type;

    switch (info.type) {
        case CO_SEQGEN:
            obj->object = new_seq_gen(info.p.gen.use_reset_flag,
                    info.p.gen.use_init_flag,
                    info.p.gen.init_seq);
            break;
        case CO_SEQREC:
            obj->object = new_seq_rec(info.p.rec.algo,
                    info.p.rec.use_reset_flag,
                    info.p.rec.use_init_flag,
                    info.p.rec.history_length,
                    info.p.rec.reset_msec,
                    info.p.rec.latent_error_paths, NULL);
            break;
        case CO_POF:
            obj->object = new_pof(info.p.pof.max_delay,
                    info.p.pof.take_any_time,
                    info.p.pof.buffer_size);
            break;
        case CO_REPL:
            obj->object = new_replicate();
            break;

    }
    if (obj->object == NULL) {
        fprintf(stderr, "error creating object '%s'\n", key);
        return 0;
    } else {
        hashmap_insert(state->objects, strdup(key), obj);
        return 1;
    }
}

static int delete_cb(const char *key, void *value, void *userdata)
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
            delete_pof(obj->object);
            break;
        case CO_REPL:
            delete_replicate(obj->object);
            break;
    }
    free(obj);
    return 1;
}

struct HashMap *parse_objects(struct IniSection *objects_section)
{
    struct ForeachState state = {0};
    state.objects = new_hashmap(13, delete_cb, NULL);

    if (!hashmap_foreach(objects_section->contents, object_cb, &state)) {
        fprintf(stderr, "error in the objects section\n");
        delete_hashmap(state.objects);
        return NULL;
    } else {
        return state.objects;
    }
}

const char *confobject_name_from_type(enum ConfObjectType type)
{
    switch (type) {
        case CO_SEQGEN:
            return "SeqGen";
        case CO_SEQREC:
            return "SeqRcvy";
        case CO_POF:
            return "Pof";
        case CO_REPL:
            return "Replicate";
    }
    return NULL;
}

