// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_object.h"
#include "conf_utils.h"
#include "inifile.h"
#include "log.h"
#include "pof.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

struct ForeachState {
    struct HashMap *objects;
};

struct ObjectInfo {
    const char *name;
    int auto_mip_level;
    enum PipelineObjectType type;
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
            struct RecoveryDiagnosticConf diag;
            enum SequenceRecoveryAlgorithm algo;
        } rec;
        struct {
            unsigned max_delay_ms;
            unsigned take_any_time_ms;
            unsigned buffer_size;
        } pof;
    } p;
};

static void set_default_parameters(struct ObjectInfo *info)
{
    info->auto_mip_level = -1;
    switch (info->type) {
        case PO_SEQGEN:
            info->p.gen.use_reset_flag = false;
            info->p.gen.use_init_flag = false;
            info->p.gen.init_seq = 0x8000; // based on the old code
            break;
        case PO_SEQREC:
            info->p.rec.use_reset_flag = false;
            info->p.rec.use_init_flag = false;
            info->p.rec.history_length = 2;
            info->p.rec.reset_msec = 2000;
            info->p.rec.diag.latent_error_paths = 2;
            info->p.rec.diag.latent_error_period = 0;
            info->p.rec.algo = RCVY_Vector;
            break;
        case PO_POF:
            info->p.pof.max_delay_ms = 20;
            info->p.pof.take_any_time_ms = 2000;
            info->p.pof.buffer_size = 2; // = info->p.rec.history_length
            break;
        case PO_REPL:
            // nothing to init
            break;
    }
}

static bool token_cb(char *str, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("object %s error: " msg,                          \
                info->name, ##__VA_ARGS__);                         \
        return false;                                               \
    } while (0)

    struct ObjectInfo *info = (struct ObjectInfo *)userdata;

    if (info->type) {
        char *key, *val;
        if (parse_assignment(str, &key, &val)) {
            if (strcmp(key, "AutoMIP") == 0) {
                // At the moment for Seqgen and Seqrec only
                int auto_mip_level;
                char err;
                if (sscanf(val, "%i%c", &auto_mip_level, &err) != 1) {
                    THROW("invalid automatic MIP level: '%s'", val);
                }
                if (auto_mip_level > 7 || auto_mip_level < 0) {
                    THROW("'%s' value '%s' invalid (valid range is 0-7)", key, val);
                }
                info->auto_mip_level = auto_mip_level;
                return true;
            }
            switch (info->type) {
                case PO_SEQGEN:
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
                case PO_SEQREC:
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
                        info->p.rec.diag.latent_error_paths = path;
                    } else if (strcmp(key, "frerSeqRcvyLatentErrorPeriod") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid latent error period '%s'", val);
                        }
                        info->p.rec.diag.latent_error_period = msec;
                    } else if (strcmp(key, "frerSeqRcvyLatentResetPeriod") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid latent error reset period '%s'", val);
                        }
                        info->p.rec.diag.latent_reset_period = msec;
                    } else if (strcmp(key, "frerSeqRcvyLatentErrorDifference") == 0) {
                        unsigned pkts;
                        char err;
                        if (sscanf(val, "%i%c", &pkts, &err) != 1) {
                            THROW("invalid latent error period '%s'", val);
                        }
                        info->p.rec.diag.latent_error_difference = pkts;
                    } else if (strcmp(key, "frerSeqRcvyOutageThreshold") == 0) {
                        unsigned pkts;
                        char err;
                        if (sscanf(val, "%i%c", &pkts, &err) != 1) {
                            THROW("invalid outage threshold '%s'", val);
                        }
                        info->p.rec.diag.outage_threshold = pkts;
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
                case PO_POF:
                    if (strcmp(key, "TakeAnyTime") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid take any time '%s'", val);
                        }
                        info->p.pof.take_any_time_ms = msec;
                    } else if (strcmp(key, "MaxDelay") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid max delay time '%s'", val);
                        }
                        info->p.pof.max_delay_ms = msec;
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
                case PO_REPL:
                    THROW("invalid parameter '%s' for replication", key);
                    break;
            }
        } else {
            THROW("object parameter '%s' has invalid format", str);
        }
    } else {
        if (strcmp(str, "SeqGen") == 0) {
            info->type = PO_SEQGEN;
        } else if (strcmp(str, "SeqRcvy") == 0) {
            info->type = PO_SEQREC;
        } else if (strcmp(str, "Pof") == 0) {
            info->type = PO_POF;
        } else if (strcmp(str, "Replicate") == 0) {
            info->type = PO_REPL;
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
    char *desc = (char *)value;
    struct ForeachState *state = (struct ForeachState *)userdata;
    struct ObjectInfo info;
    memset(&info, 0, sizeof(info));
    info.name = key;

    if (!foreach_tokens(desc, token_cb, &info)) {
        log_error("parsing parameters failed for object '%s'", key);
        return 0;
    }

    struct PipelineObject *obj = NULL;
    switch (info.type) {
        case PO_SEQGEN:
            obj = new_seq_gen(key, info.p.gen.use_reset_flag,
                    info.p.gen.use_init_flag,
                    info.p.gen.init_seq);
            break;
        case PO_SEQREC:
            obj = new_seq_rec(key, info.p.rec.algo,
                    info.p.rec.use_reset_flag,
                    info.p.rec.use_init_flag,
                    info.p.rec.history_length,
                    info.p.rec.reset_msec,
                    &info.p.rec.diag);
            break;
        case PO_POF:
            obj = new_pof(key, info.p.pof.max_delay_ms,
                    info.p.pof.take_any_time_ms,
                    info.p.pof.buffer_size);
            break;
        case PO_REPL:
            obj = new_replicate(key);
            break;

    }
    if (obj == NULL) {
        log_error("creating object '%s' failed", key);
        return 0;
    } else {
        obj->auto_mip_level = info.auto_mip_level;
        hashmap_insert(state->objects, obj->name, obj);
        log_info("object %s type %s", obj->name, pipelineobject_name_from_type(obj->type));
        return 1;
    }
}

static int delete_cb(const char *key, void *value, void *userdata)
{
    (void)key; // this is obj->name, freed by delete_pipeline_object()
    struct PipelineObject *obj = (struct PipelineObject *)value;
    (void)userdata;
    delete_pipeline_object(obj);
    return 1;
}

struct HashMap *parse_objects(const struct IniSection *objects_section)
{
    struct ForeachState state = {0};
    state.objects = new_hashmap(13, delete_cb, NULL);

    if (!hashmap_foreach(objects_section->contents, object_cb, &state)) {
        log_error("error in the objects section");
        delete_hashmap(state.objects);
        return NULL;
    } else {
        return state.objects;
    }
}

