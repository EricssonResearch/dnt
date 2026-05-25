// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_object.h"
#include "conf_utils.h"
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
    }; // anonymous union needs gnu99 or c11
};

static void set_default_parameters(struct ObjectInfo *info)
{
    info->auto_mip_level = -1;
    switch (info->type) {
        case PIPEOBJ_SEQGEN:
            info->gen.use_reset_flag = false;
            info->gen.use_init_flag = false;
            info->gen.init_seq = 0x8000; // based on the old code
            break;
        case PIPEOBJ_SEQREC:
            info->rec.use_reset_flag = false;
            info->rec.use_init_flag = false;
            info->rec.history_length = 2;
            info->rec.reset_msec = 2000;
            info->rec.diag.admin_latent_error_paths = 2;
            info->rec.diag.latent_error_period = 0;
            info->rec.algo = RCVY_Vector;
            break;
        case PIPEOBJ_POF:
            info->pof.max_delay_ms = 20;
            info->pof.take_any_time_ms = 2000;
            info->pof.buffer_size = 2; // = info->p.rec.history_length
            break;
        case PIPEOBJ_REPL:
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
                case PIPEOBJ_SEQGEN:
                    if (strcmp(key, "ResetFlag") == 0) {
                        int reset = read_boolean(val);
                        if (reset < 0) {
                            THROW("invalid reset flag");
                        }
                        info->gen.use_reset_flag = reset;
                    } else if (strcmp(key, "InitSeqFlag") == 0) {
                        int init = read_boolean(val);
                        if (init < 0) {
                            THROW("invalid init flag");
                        }
                        info->gen.use_init_flag = init;
                    } else if (strcmp(key, "InitSeqStart") == 0) {
                        unsigned seq;
                        char err;
                        if (sscanf(val, "%i%c", &seq, &err) != 1) {
                            THROW("invalid init sequence number '%s'", val);
                        }
                        info->gen.init_seq = seq;
                    } else {
                        THROW("invalid parameter '%s' for sequence generator", key);
                    }
                    break;
                case PIPEOBJ_SEQREC:
                    if (strcmp(key, "ResetFlag") == 0) {
                        int reset = read_boolean(val);
                        if (reset < 0) {
                            THROW("invalid reset flag");
                        }
                        info->rec.use_reset_flag = reset;
                    } else if (strcmp(key, "InitSeqFlag") == 0) {
                        int init = read_boolean(val);
                        if (init < 0) {
                            THROW("invalid init flag");
                        }
                        info->rec.use_init_flag = init;
                    } else if (strcmp(key, "frerSeqRcvyHistoryLength") == 0) {
                        unsigned hlen;
                        char err;
                        if (sscanf(val, "%i%c", &hlen, &err) != 1) {
                            THROW("invalid history length '%s'", val);
                        }
                        info->rec.history_length = hlen;
                    } else if (strcmp(key, "frerSeqRcvyResetMSec") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid reset msec '%s'", val);
                        }
                        info->rec.reset_msec = msec;
                    } else if (strcmp(key, "frerSeqRcvyLatentErrorPaths") == 0) {
                        unsigned path;
                        char err;
                        if (sscanf(val, "%i%c", &path, &err) != 1) {
                            THROW("invalid latent error paths '%s'", val);
                        }
                        info->rec.diag.admin_latent_error_paths = path;
                    } else if (strcmp(key, "frerSeqRcvyLatentErrorPeriod") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid latent error period '%s'", val);
                        }
                        info->rec.diag.latent_error_period = msec;
                    } else if (strcmp(key, "frerSeqRcvyLatentResetPeriod") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid latent error reset period '%s'", val);
                        }
                        info->rec.diag.latent_reset_period = msec;
                    } else if (strcmp(key, "frerSeqRcvyLatentErrorDifference") == 0) {
                        unsigned pkts;
                        char err;
                        if (sscanf(val, "%i%c", &pkts, &err) != 1) {
                            THROW("invalid latent error period '%s'", val);
                        }
                        info->rec.diag.latent_error_difference = pkts;
                    } else if (strcmp(key, "frerSeqRcvyOutageThreshold") == 0) {
                        unsigned pkts;
                        char err;
                        if (sscanf(val, "%i%c", &pkts, &err) != 1) {
                            THROW("invalid outage threshold '%s'", val);
                        }
                        info->rec.diag.outage_threshold = pkts;
                    } else if (strcmp(key, "frerSeqRcvyAlgorithm") == 0) {
                        if (strcmp(val, "Vector") == 0) {
                            info->rec.algo = RCVY_Vector;
                        } else if (strcmp(val, "SeamlessVector") == 0) {
                            info->rec.algo = RCVY_SeamlessVector;
                        } else if (strcmp(val, "Match") == 0) {
                            info->rec.algo = RCVY_Match;
                        } else {
                            THROW("invalid recovery algorithm '%s'", val);
                        }
                    } else {
                        THROW("invalid parameter '%s' for sequence recovery", key);
                    }
                    break;
                case PIPEOBJ_POF:
                    if (strcmp(key, "TakeAnyTime") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid take any time '%s'", val);
                        }
                        info->pof.take_any_time_ms = msec;
                    } else if (strcmp(key, "MaxDelay") == 0) {
                        unsigned msec;
                        char err;
                        if (sscanf(val, "%i%c", &msec, &err) != 1) {
                            THROW("invalid max delay time '%s'", val);
                        }
                        info->pof.max_delay_ms = msec;
                    } else if (strcmp(key, "BufferSize") == 0) {
                        unsigned size;
                        char err;
                        if (sscanf(val, "%i%c", &size, &err) != 1) {
                            THROW("invalid max delay time '%s'", val);
                        }
                        info->pof.buffer_size = size;
                    } else {
                        THROW("invalid parameter '%s' for packet ordering function", key);
                    }
                    break;
                case PIPEOBJ_REPL:
                    THROW("invalid parameter '%s' for replication", key);
                    break;
            }
        } else {
            THROW("object parameter '%s' has invalid format", str);
        }
    } else {
        if (strcmp(str, "SeqGen") == 0) {
            info->type = PIPEOBJ_SEQGEN;
        } else if (strcmp(str, "SeqRcvy") == 0) {
            info->type = PIPEOBJ_SEQREC;
        } else if (strcmp(str, "Pof") == 0) {
            info->type = PIPEOBJ_POF;
        } else if (strcmp(str, "Replicate") == 0) {
            info->type = PIPEOBJ_REPL;
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
        case PIPEOBJ_SEQGEN:
            obj = new_seq_gen(key, info.gen.use_reset_flag,
                    info.gen.use_init_flag,
                    info.gen.init_seq);
            break;
        case PIPEOBJ_SEQREC:
            obj = new_seq_rec(key, info.rec.algo,
                    info.rec.use_reset_flag,
                    info.rec.use_init_flag,
                    info.rec.history_length,
                    info.rec.reset_msec,
                    &info.rec.diag);
            break;
        case PIPEOBJ_POF:
            obj = new_pof(key, info.pof.max_delay_ms,
                    info.pof.take_any_time_ms,
                    info.pof.buffer_size);
            break;
        case PIPEOBJ_REPL:
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

bool parse_objects(struct HashMap *objects, const struct IniSection *objects_section)
{
    struct ForeachState state = {0};
    state.objects = objects;

    if (!hashmap_foreach(objects_section->contents, object_cb, &state)) {
        log_error("error in the objects section");
        return false;
    } else {
        return true;
    }
}

