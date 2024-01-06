// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "object.h"
#include "pof.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"

#include <stdlib.h>

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
