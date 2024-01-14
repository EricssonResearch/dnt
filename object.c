// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "object.h"
#include "pof.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

struct PipelineObject *delete_pipeline_object(struct PipelineObject *obj)
{
    switch (obj->type) {
        case PO_SEQGEN:
            delete_seq_gen(obj);
            break;
        case PO_SEQREC:
            delete_seq_rec(obj);
            break;
        case PO_POF:
            delete_pof(obj);
            break;
        case PO_REPL:
            delete_replicate(obj);
            break;
    }
    return NULL;
}

const char *pipelineobject_name_from_type(enum PipelineObjectType type)
{
    switch (type) {
        case PO_SEQGEN:
            return "SeqGen";
        case PO_SEQREC:
            return "SeqRcvy";
        case PO_POF:
            return "Pof";
        case PO_REPL:
            return "Replicate";
    }
    return NULL;
}

char *sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
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

