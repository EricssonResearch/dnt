// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OBJECT_INTERNAL

#include "object.h"
#include "log.h"
#include "pof.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(OBJECT, INFO);

void pipeline_object_ref(struct PipelineObject *obj)
{
    int refcount = __atomic_add_fetch(&obj->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s ref refcount %d", obj->name, refcount);
}

void pipeline_object_unref(struct PipelineObject *obj)
{
    int refcount = __atomic_sub_fetch(&obj->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s unref refcount %d", obj->name, refcount);

    if (refcount == 0) {
        switch (obj->type) {
            case PIPEOBJ_SEQGEN:
                delete_seq_gen(obj);
                break;
            case PIPEOBJ_SEQREC:
                delete_seq_rec(obj);
                break;
            case PIPEOBJ_POF:
                delete_pof(obj);
                break;
            case PIPEOBJ_REPL:
                delete_replicate(obj);
                break;
        }
    }
}

const char *pipelineobject_get_name(const struct PipelineObject *obj)
{
    return obj->name;
}

const char *pipelineobject_name_from_type(enum PipelineObjectType type)
{
    switch (type) {
        case PIPEOBJ_SEQGEN:
            return "SeqGen";
        case PIPEOBJ_SEQREC:
            return "SeqRcvy";
        case PIPEOBJ_POF:
            return "Pof";
        case PIPEOBJ_REPL:
            return "Replicate";
    }
    return NULL;
}

char *pipelineobject_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
{
    (void)record_sep;
    struct JsonValue *oname = json_object_get_string(json, "name");
    struct JsonValue *otype = json_object_get_string(json, "type");

    if (oname && otype) {
        char *ostate = NULL;
        if (strcmp(otype->v.string, "pof") == 0) {
            ostate = pof_sprintf_state_json(json, record_sep, line_sep);
        } else if (strcmp(otype->v.string, "replicate") == 0) {
            ostate = repl_sprintf_state_json(json, record_sep, line_sep);
        } else if (strcmp(otype->v.string, "seqgen") == 0) {
            ostate = seq_gen_sprintf_state_json(json, record_sep, line_sep);
        } else if (strcmp(otype->v.string, "seqrec") == 0) {
            ostate = seq_rec_sprintf_state_json(json, record_sep, line_sep);
        } else {
            //TODO print the keys?
            ostate = strdup("<state of unknown object type>");
        }

        char *ret = strdup_printf("Object %s type %s%s%s", oname->v.string, otype->v.string, line_sep, ostate);
        free(ostate);
        return ret;
    } else {
        return strdup("<invalid object state>");
    }

}

void pipelineobject_print_info(const struct PipelineObject *obj, FILE *cmd_w)
{
    fprintf(cmd_w, "object \033[36m%s\033[0m type \033[1m%s\033[0m refcount %d\n",
            obj->name, pipelineobject_name_from_type(obj->type), obj->reference_count);
    obj->print_info(obj, cmd_w);
}
