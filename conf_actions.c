// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_actions.h"
#include "action.h"
#include "conf_object.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "hashmap.h"
#include "header.h"
#include "if_oam_cmd.h"
#include "inifile.h"
#include "interface.h"
#include "log.h"
#include "oam.h"
#include "packet.h"
#include "parsetree.h"
#include "pipeline.h"
#include "protocol.h"
#include "seq_gen.h"
#include "utils.h"
#include "value.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <arpa/inet.h> /* ntohs() */

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

#define MAX_DEPTH 10

// when adding a new action to R2DTWO, start with adding it to this enum
enum ConfActionType {
    CA_UNDEF,
    CA_ADD,
    CA_DEL,
    CA_DELAY,
    CA_DROP,
    CA_EDIT,
    CA_ELIM,
    CA_FILTEROAM,
    CA_JUMP,
    CA_MEPSTART,
    CA_MEPSTOP,
    CA_MIP,
    CA_POF,
    CA_READSEQ,
    CA_READTSTAMP,
    CA_REPL,
    CA_SEND,
    CA_SEQGEN,
    CA_TTLCHECK,
    CA_TTLREDUCE,
    CA_WRITESEQ,
    CA_WRITETSTAMP,
};

enum BeforeAfter {
    ADD_UNKNOWN,
    ADD_BEFORE,
    ADD_AFTER,
};

enum ConfVariableType {
    CVT_UNDEF,
    CVT_FIELD, // header field
    // the ones below are rhs-only
    CVT_CONST, // constant
    CVT_IFACE, // interface property
};

struct ConfVariable {
    enum ConfVariableType type;
    enum ProtocolFieldType value_type;
    struct Value value; // describes the passed value, and stores constant
    union {
        struct {
            struct HeaderField *field; // state for read/write
        } header;
        struct {
            struct Interface *iface; // state for read
            char *property;
        } iface;
    } v;
};

struct ConfAssignment {
    struct ConfVariable lhs;
    struct ConfVariable rhs;
    value_consumer *write;
    value_producer *read;
    char *text;
    enum ProtocolID lhs_protoid;
    struct ConfAssignment *next;
};

struct ReplicateList {
    char *name;
    struct ConfAction *actions;
    struct ReplicateList *next;
};

// compiler-internal representation of an action
struct ConfAction {
    enum ConfActionType type;
    char *text;
    struct ConfAction *next;
    // action-specific data
    union {
        struct {
            char *newname;
            enum ProtocolID id;
            struct HeaderDescriptor *pos; // relative to this header
            enum BeforeAfter beforeafter;
            unsigned pos_idx;
            unsigned len;
            struct ConfAssignment *assignments;
            bool was_add;
        } add;
        struct {
            struct HeaderDescriptor *hdr;
            unsigned idx;
        } del;
        struct {
            float delay_value;
            bool offload;
        } delay;
        struct {
            struct ConfAssignment *assignments;
        } edit;
        struct {
            struct PipelineObject *rec;
            char *pipename;
        } elim;
        struct {
            struct HeaderField *field;
        } filteroam;
        struct {
            char *pipename;
        } jump;
        struct {
            struct HeaderField *field;
        } meta; // read/write seq/tstamp
        struct {
            char *name;
            char *stream;
            int level;
            struct PipelineObject *obj; // NULL if no associated object
            bool auto_generated;
        } oam;
        struct {
            struct PipelineObject *pof;
        } pof;
        struct {
            struct ReplicateList *pipelines;
            struct PipelineObject *replobj;
        } repl;
        struct {
            struct Interface *iface;
        } send;
        struct {
            struct PipelineObject *gen;
        } seq;
        struct {
            struct HeaderField *field;
        } ttl;
    } ; // anonymous union needs gnu99 or c11
};

struct MustWriteField {
    const struct HeaderDescriptor *header;
    const struct ProtocolField *field;
    struct MustWriteField *next;
};

struct StageState {
    const char *stream;
    struct ConfAction *actions;
    struct HeaderDescriptor *headers;
    const struct HashMap *ifaces;
    const struct HashMap *objects;
    const struct IniSection *streams_sec;
    bool had_delay_offload;
    bool had_final;
    bool seq_set; // true if we had an action that sets packet->sequence
    bool ttl_set; // true if we had an action that sets packet->ttl
    struct HeaderDescriptor *needs_ttlcheck; // points to the header we automatically put a TTLReduce on
    struct MustWriteField *must_write;
    unsigned depth; // limit recursion depth with JUMP and REPLICATE
};


static void replicatelist_push(struct ReplicateList **list, char *name, struct ConfAction *actions)
{
    struct ReplicateList *l = calloc_struct(ReplicateList);
    l->name = name;
    l->actions = actions;
    l->next = *list;
    *list = l;
}

// shallow copy, we are not interested in the match lists
static struct HeaderDescriptor *copy_header_list(const struct HeaderDescriptor *headers)
{
    struct HeaderDescriptor *ret = NULL;
    struct HeaderDescriptor *r = NULL;
    const struct HeaderDescriptor *h = headers;

    while (h) {
        struct HeaderDescriptor *w = calloc_struct(HeaderDescriptor);
        if (r) {
            r->next = w;
            r = w;
        } else {
            ret = r = w;
        }
        w->name = strdup(h->name);
        w->id = h->id;

        h = h->next;
    }

    return ret;
}

// make a copy of @stst->must_write list, adjusting the header references to @newheaders
// we assume that @newheaders = copy_header_list(@stst->headers)
static struct MustWriteField *copy_mustwrite_list(const struct StageState *stst, const struct HeaderDescriptor *newheaders)
{
    struct MustWriteField *ret = NULL;

    for (struct MustWriteField *mw=stst->must_write; mw; mw=mw->next) {
        struct MustWriteField *n = calloc_struct(MustWriteField);
        n->field = mw->field;
        n->next = ret;
        ret = n;

        const struct HeaderDescriptor *oh = stst->headers;
        const struct HeaderDescriptor *nh = newheaders;
        while (oh) {
            if (oh == mw->header) {
                n->header = nh;
                break;
            }
            oh = oh->next;
            nh = nh->next;
        }
        if (!oh) {
            log_error("stream %s has a must_write entry referencing non-existing header (field %s)",
                        stst->stream, mw->field->name);
            struct MustWriteField *k = ret;
            while (k) {
                struct MustWriteField *d = k;
                k = k->next;
                free(d);
            }
            return NULL;
        }
    }

    REVERSE_LIST(ret);
    return ret;
}

static void delete_must_write_list(struct MustWriteField *must_write)
{
    while (must_write) {
        struct MustWriteField *del = must_write;
        must_write = must_write->next;
        free(del);
    }
}

// @returns the position of @pos in the linked list @headers
// @pos must be in the linked list!
static unsigned header_index(const struct HeaderDescriptor *headers, const struct HeaderDescriptor *pos)
{
    unsigned pos_idx = 0;
    for (const struct HeaderDescriptor *h=headers; h!=pos; h=h->next) pos_idx++;
    return pos_idx;
}

static struct HeaderField *header_get_field_of_type(struct HeaderDescriptor *hdr, unsigned hdr_idx,
        enum ProtocolFieldType type)
{
    const struct ProtocolField * field = protocol_get_field_by_type(hdr->id, type);
    if (field)
        return new_headerfield(hdr_idx, field);
    else
        return NULL;
}

// concatenate @a and @b, return the beginning of the new list
// both @a and @b can be NULL
static struct ConfAction *confaction_concat(struct ConfAction *a, struct ConfAction *b)
{
    if (a == NULL) return b;

    struct ConfAction *p = a;
    while (p->next) p = p->next;
    p->next = b;
    return a;
}

// swap @ca with @ca->next, return the new beginning of the list
// doesn't do anything if next is NULL
static struct ConfAction *confaction_swap_with_next(struct ConfAction *ca)
{
    if (ca->next == NULL) return ca;
    struct ConfAction *n = ca->next;
    ca->next = n->next;
    n->next = ca;
    return n;
}

static const char *confaction_name_from_type(enum ConfActionType type)
{
    switch (type) {
        case CA_UNDEF:
            return "Undefined";
        case CA_ADD:
            return "Add";
        case CA_DEL:
            return "Del";
        case CA_DELAY:
            return "Delay";
        case CA_DROP:
            return "Drop";
        case CA_EDIT:
            return "Edit";
        case CA_ELIM:
            return "Eliminate";
        case CA_FILTEROAM:
            return "FilterOAM";
        case CA_JUMP:
            return "Jump";
        case CA_MEPSTART:
            return "MEPStart";
        case CA_MEPSTOP:
            return "MEPStop";
        case CA_MIP:
            return "MIP";
        case CA_POF:
            return "POF";
        case CA_READSEQ:
            return "ReadSeq";
        case CA_READTSTAMP:
            return "ReadTstamp";
        case CA_REPL:
            return "Replicate";
        case CA_SEND:
            return "Send";
        case CA_SEQGEN:
            return "SeqGen";
        case CA_TTLCHECK:
            return "TTLCheck";
        case CA_TTLREDUCE:
            return "TTLReduce";
        case CA_WRITESEQ:
            return "WriteSeq";
        case CA_WRITETSTAMP:
            return "WriteTstamp";
    }
    return NULL;
}

static const char *variabletype_name_from_type(enum ConfVariableType type)
{
    switch (type) {
        case CVT_UNDEF:
            return "Undefined";
        case CVT_FIELD:
            return "Field";
        case CVT_CONST:
            return "Constant";
        case CVT_IFACE:
            return "Interface";
    }
    return NULL;
}

static void init_confvariable_full(struct ConfVariable *v,
        enum ConfVariableType type, enum ProtocolFieldType value_type,
        unsigned bitoffset, unsigned bitcount)
{
    v->type = type;
    v->value_type = value_type;
    v->value = init_value(bitoffset, bitcount);
}

static void init_confvariable(struct ConfVariable *v,
        enum ConfVariableType type, const struct ProtocolField *field)
{
    v->type = type;
    v->value_type = field->type;
    v->value = init_value(field->bitoffset, field->bitcount);
}


// @returns false on error
static bool process_assignment_lhs(struct StageState *stst, struct ConfAssignment *assign, char *string)
{
#define THROW(msg, ...)                                                 \
    do {                                                                \
        log_error("assignment lhs: " msg, ##__VA_ARGS__);               \
        return false;                                                   \
    } while (0)

    struct ConfVariable *lhs = &assign->lhs;
    char *hdr, *field;
    if (parse_fieldname(string, &hdr, &field)) {
        struct HeaderDescriptor *h = header_list_find_by_name(stst->headers, hdr);
        if (h == NULL) {
            THROW("no header named '%s' in the packet", hdr);
        }
        if (header_list_find_by_name(h->next, hdr)) {
            THROW("header name '%s' is ambiguous", hdr);
        }
        const struct ProtocolField *f = protocol_get_field_by_name(h->id, field);
        if (f == NULL) {
            THROW("header '%s' has no field named '%s'", hdr, field);
        }
        init_confvariable(lhs, CVT_FIELD, f);
        lhs->v.header.field = new_headerfield(header_index(stst->headers, h), f);
        assign->lhs_protoid = h->id;

        // remove the must_write entry if we are writing that header field
        struct MustWriteField *mw_last = NULL;
        struct MustWriteField *mw = stst->must_write;
        while (mw) {
            if (h == mw->header && f == mw->field) {
                if (mw_last) {
                    mw_last->next = mw->next;
                    free(mw);
                    mw = mw_last->next;
                } else {
                    stst->must_write = mw->next;
                    free(mw);
                    mw = stst->must_write;
                }
            } else {
                mw_last = mw;
                mw = mw->next;
            }
        }
    } else {
        THROW("left-hand-side '%s' of the assignment is invalid", string);
    }
    return true;
#undef THROW
}

// @returns false on error
static bool process_assignment_rhs(struct StageState *stst, struct ConfAssignment *assign, char *string)
{
#define THROW(msg, ...)                                                 \
    do {                                                                \
        log_error("assignment rhs: " msg, ##__VA_ARGS__);               \
        return false;                                                   \
    } while (0)

    const struct ConfVariable *lhs = &assign->lhs;
    struct ConfVariable *rhs = &assign->rhs;
    char *key, *val;
    if (parse_fieldname(string, &key, &val)) {
        log_debug("rhs has a dot '%s' . '%s'", key, val);
        struct HeaderDescriptor *h = header_list_find_by_name(stst->headers, key);
        if (h) {
            if (header_list_find_by_name(h->next, key)) {
                THROW("header name '%s' is ambiguous", key);
            }
            const struct ProtocolField *f = protocol_get_field_by_name(h->id, val);
            if (f) {
                log_debug("rhs is a header field");
                if (lhs->value_type != f->type) {
                    THROW("types of left-hand-side %s and right-hand-side %s don't match",
                            fieldtype_name_from_type(lhs->value_type), fieldtype_name_from_type(f->type));
                }
                init_confvariable(rhs, CVT_FIELD, f);
                struct HeaderField *hf = new_headerfield(header_index(stst->headers, h), f);
                rhs->v.header.field = hf;

                assign->read = header_get_field_reader(&lhs->value, hf);
                if (assign->read == NULL) {
                    //TODO can we print the name of lhs?
                    THROW("cannot read field %s.%s into the left-hand-side expression", key, val);
                }

                assign->write = header_get_field_writer(lhs->v.header.field, &rhs->value);
                if (assign->write == NULL) {
                    //TODO can we print the name of lhs?
                    THROW("cannot write field %s.%s into the left-hand-side expression", key, val);
                }
                return true;
            } else {
                THROW("header %s has no field %s", key, val);
            }
        }

        struct Interface *iface = hashmap_find(stst->ifaces, key);
        if (iface) {
            log_debug("rhs is an interface");
            if (iface->get_property_reader) {

                assign->read = iface->get_property_reader(iface, val, lhs->value_type, &lhs->value);
                if (assign->read == NULL) {
                    THROW("interface %s has no property named '%s'", iface->name, val);
                }
                init_confvariable_full(rhs, CVT_IFACE,
                        lhs->value_type, lhs->value.bitoffset%8, lhs->value.bitcount);
                rhs->v.iface.iface = iface;
                rhs->v.iface.property = strdup(val);

                assign->write = header_get_field_writer(lhs->v.header.field, &rhs->value);
                if (assign->write == NULL) {
                    //TODO can we print the name of lhs?
                    THROW("cannot write interface property %s.%s into the left-hand-side expression", key, val);
                }
                return true;
            } else {
                THROW("interface %s has no queryable property", iface->name);
            }
        }

        // if nothing matched, restore the dot
        val[-1] = '.';
    }

    //log_debug("rhs may be a constant...");
    // constant doesn't have a read function just a value
    init_confvariable_full(rhs, CVT_CONST, FT_UNKNOWN, lhs->value.bitoffset, lhs->value.bitcount);
    if (read_constant(&rhs->value, assign->lhs_protoid, lhs->value_type, string)) {
        log_debug("rhs is a constant");
        rhs->value_type = lhs->value_type;
        assign->read = NULL;

        assign->write = header_get_field_writer(lhs->v.header.field, &rhs->value);
        if (assign->write == NULL) {
            THROW("cannot write constant '%s' into the left-hand-side expression", string);
        }
        return true;
    } else {
        THROW("failed to parse '%s' as a header field, interface property, or a constant", string);
    }
#undef THROW
}

static bool process_stage(char *stage, void *userdata);

// here we process the parameters for the action individually
static bool process_token(char *token, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("stream '%s' action '%s': " msg,                      \
                stst->stream, stst->actions->text, ##__VA_ARGS__);  \
        return false;                                               \
    } while (0)

    struct StageState *stst = userdata;
    struct ConfAction *newaction = stst->actions;

    switch (newaction->type) {
        case CA_UNDEF:
            if        (strcmp(token, "after") == 0) {
                newaction->type = CA_ADD;
                newaction->add.beforeafter = ADD_AFTER;
            } else if (strcmp(token, "before") == 0) {
                newaction->type = CA_ADD;
                newaction->add.beforeafter = ADD_BEFORE;
            } else if (strcmp(token, "del") == 0) {
                newaction->type = CA_DEL;
            } else if (strcmp(token, "delay") == 0) {
                newaction->type = CA_DELAY;
            } else if (strcmp(token, "drop") == 0) {
                newaction->type = CA_DROP;
            } else if (strcmp(token, "edit") == 0) {
                newaction->type = CA_EDIT;
            } else if (strcmp(token, "eliminate") == 0) {
                newaction->type = CA_ELIM;
            } else if (strcmp(token, "jump") == 0) {
                newaction->type = CA_JUMP;
            } else if (strcmp(token, "mep-start") == 0) {
                newaction->type = CA_MEPSTART;
                newaction->oam.level = -1;
            } else if (strcmp(token, "mep-stop") == 0) {
                newaction->type = CA_MEPSTOP;
                newaction->oam.level = -1;
            } else if (strcmp(token, "mip") == 0) {
                newaction->type = CA_MIP;
                newaction->oam.level = -1;
            } else if (strcmp(token, "pof") == 0) {
                newaction->type = CA_POF;
            } else if (strcmp(token, "readseq") == 0) {
                newaction->type = CA_READSEQ;
            } else if (strcmp(token, "readtstamp") == 0) {
                newaction->type = CA_READTSTAMP;
            } else if (strcmp(token, "replicate") == 0) {
                newaction->type = CA_REPL;
            } else if (strcmp(token, "send") == 0) {
                newaction->type = CA_SEND;
            } else if (strcmp(token, "seqgen") == 0) {
                newaction->type = CA_SEND;
            } else if (strcmp(token, "ttlcheck") == 0) {
                newaction->type = CA_TTLCHECK;
            } else if (strcmp(token, "ttlreduce") == 0) {
                newaction->type = CA_TTLREDUCE;
            } else if (strcmp(token, "writeseq") == 0) {
                newaction->type = CA_WRITESEQ;
            } else if (strcmp(token, "writetstamp") == 0) {
                newaction->type = CA_WRITETSTAMP;
            } else {
                struct PipelineObject *obj = hashmap_find(stst->objects, token);
                if (obj) {
                    switch (obj->type) {
                        case PO_SEQGEN:
                            newaction->type = CA_SEQGEN;
                            newaction->seq.gen = obj;
                            break;
                        case PO_SEQREC:
                            newaction->type = CA_ELIM;
                            newaction->elim.rec = obj;
                            break;
                        case PO_POF:
                            newaction->type = CA_POF;
                            newaction->pof.pof = obj;
                            break;
                        case PO_REPL:
                            newaction->type = CA_REPL;
                            newaction->repl.replobj = obj;
                            break;
                    }
                } else {
                    char *pstring = inisection_get(stst->streams_sec, token);
                    if (pstring) {
                        newaction->type = CA_JUMP;
                        newaction->jump.pipename = strdup(token);
                    } else {
                        THROW("'%s' does not name an action, object or action list", token);
                    }
                }
            }
            break;
        case CA_ADD:
            if (newaction->add.pos == NULL) {
                struct HeaderDescriptor *pos = header_list_find_by_name(stst->headers, token);
                if (pos == NULL) {
                    THROW("no header named '%s' in the packet", token);
                }
                if (header_list_find_by_name(pos->next, token)) {
                    THROW("header name '%s' is ambiguous", token);
                }
                newaction->add.pos = pos;
            } else if (newaction->add.was_add == false) {
                if (strcmp(token, "add") == 0) {
                    newaction->add.was_add = true;
                } else {
                    THROW("the 'add' keyword is mising");
                }
            } else if (newaction->add.newname == NULL) {
                newaction->add.newname = strdup(token);
                char *type = header_type_from_name(token);
                newaction->add.id = protocol_id_from_type(type);
                free(type);
                if (newaction->add.id < 0) {
                    THROW("type is invalid for new header'%s'", token);
                }
                newaction->add.len = protocol_list[newaction->add.id].bytelength;
            } else {
                char *lhs, *rhs;
                struct ConfAssignment *a = calloc_struct(ConfAssignment);
                a->next = newaction->add.assignments;
                newaction->add.assignments = a;
                a->text = strdup(token);
                if (parse_assignment(token, &lhs, &rhs)) {
                    const struct ProtocolField *f = protocol_get_field_by_name(newaction->add.id, lhs);
                    if (f == NULL) {
                        THROW("header %s has no field '%s'",
                                newaction->add.newname, lhs);
                    }
                    init_confvariable(&a->lhs, CVT_FIELD, f);
                    // we don't yet have a header index here, we fix it in process_action()
                    a->lhs.v.header.field = new_headerfield(0, f);

                    if (!process_assignment_rhs(stst, a, rhs)) {
                        THROW("right-hand-side '%s' invalid", rhs);
                    }
                } else {
                    THROW("invalid field assignment '%s'", token);
                }
            }
            break;
        case CA_DEL:
            if (newaction->del.hdr == NULL) {
                struct HeaderDescriptor *del = header_list_find_by_name(stst->headers, token);
                if (del) {
                    if (header_list_find_by_name(del->next, token)) {
                        THROW("header name '%s' is ambiguous", token);
                    }
                    newaction->del.hdr = del;
                } else {
                    THROW("invalid header '%s' to delete", token);
                }
            } else {
                THROW("delete action takes only 1 parameter");
            }
            break;
        case CA_DELAY:
            if (newaction->delay.delay_value == 0) {
                char err;
                if (sscanf(token, "%f%c", &newaction->delay.delay_value, &err) != 1) {
                    THROW("invalid delay '%s'", token);
                }
            } else {
                if (newaction->delay.offload == false) {
                    if (strcmp(token, "offload") == 0) {
                        newaction->delay.offload = true;
                    } else {
                        THROW("delay action requires a delay parameter and an optional offload parameter");
                    }
                }
            }
            break;
        case CA_DROP:
            THROW("drop action doesn't take parameters");
            break;
        case CA_EDIT: {
            char *lhs, *rhs;
            struct ConfAssignment *a = calloc_struct(ConfAssignment);
            a->next = newaction->edit.assignments;
            newaction->edit.assignments = a;
            a->text = strdup(token);
            if (parse_assignment(token, &lhs, &rhs)) {
                if (!process_assignment_lhs(stst, a, lhs)) {
                    THROW("left-hand-side '%s' invalid", lhs);
                }

                if (! process_assignment_rhs(stst, a, rhs)) {
                    THROW("right-hand-side '%s' invalid", rhs);
                }
            } else {
                THROW("invalid assignment '%s'", token);
            }
            break; }
        case CA_ELIM:
            if (newaction->elim.rec == NULL) {
                struct PipelineObject *obj = hashmap_find(stst->objects, token);
                if (obj) {
                    if (obj->type == PO_SEQREC) {
                        newaction->elim.rec = obj;
                    } else {
                        THROW("first argument of eliminate must be a recovery object");
                    }
                } else {
                    THROW("first argument of eliminate must be a recovery object");
                }
            } else if (newaction->elim.pipename == NULL) {
                newaction->elim.pipename = strdup(token);
            } else {
                THROW("the arguments of eliminate are the recovery object and a pipeline");
            }
            break;
        case CA_FILTEROAM:
            // the user can't create this action, so nothing to do here
            break;
        case CA_JUMP:
            if (newaction->jump.pipename == NULL) {
                newaction->jump.pipename = strdup(token);
            } else {
                THROW("the only argument is the name of an action pipeline");
            }
            break;
        case CA_MEPSTART:
        case CA_MEPSTOP:
        case CA_MIP:
            if (newaction->oam.name == NULL) {
                newaction->oam.name = strdup(token);
                newaction->oam.stream = strdup(stst->stream);
            } else if (newaction->oam.level == -1) {
                char err;
                if (sscanf(token, "%d%c", &newaction->oam.level, &err) != 1) {
                    THROW("invalid OAM level '%s'", token);
                }
                if (newaction->oam.level < 0 ||
                        newaction->oam.level > 7) {
                    THROW("invalid OAM level %d (valid range is 0-7)",
                            newaction->oam.level);
                }
                break;
            } else if (newaction->oam.obj == NULL) {
                struct PipelineObject *obj = hashmap_find(stst->objects, token);
                if (!obj) {
                    THROW("unknown object '%s' for OAM action", token);
                }
                newaction->oam.obj = obj;
                break;
            } else {
                THROW("too many arguments");
            }
            break;
        case CA_POF:
            if (newaction->pof.pof == NULL) {
                struct PipelineObject *obj = hashmap_find(stst->objects, token);
                if (obj) {
                    if (obj->type == PO_POF) {
                        newaction->pof.pof = obj;
                    } else {
                        THROW("pof first argument must be a pof object");
                    }
                } else {
                    THROW("pof first argument must be a pof object");
                }
            } else {
                THROW("pof only takes one argument");
            }
            break;
        case CA_READSEQ:
        case CA_READTSTAMP:
        case CA_WRITESEQ:
        case CA_WRITETSTAMP:
            if (newaction->meta.field == NULL) {
                struct HeaderDescriptor *hdr = header_list_find_by_name(stst->headers, token);
                if (hdr) {
                    if (header_list_find_by_name(hdr->next, token)) {
                        THROW("header name '%s' is ambiguous", token);
                    }
                    enum ProtocolFieldType fieldtype = (newaction->type == CA_READSEQ ||
                            newaction->type == CA_WRITESEQ) ? FT_TSNSEQ : FT_TSNTSTAMP;
                    struct HeaderField *field = header_get_field_of_type(hdr,
                            header_index(stst->headers, hdr), fieldtype);
                    if (field == NULL) {
                        THROW("header '%s' doesn't have a field of type %s", hdr->name,
                                fieldtype_name_from_type(fieldtype));
                    }
                    newaction->meta.field = field;
                } else {
                    THROW("invalid header '%s'", token);
                }
            } else {
                THROW("this action only takes one argument");
            }
            break;
        case CA_REPL: {
            char *pstring = inisection_get(stst->streams_sec, token);
            if (pstring == NULL) {
                // first argument can be the name of a state object
                if (newaction->repl.replobj == NULL
                        && newaction->repl.pipelines == NULL) {
                    struct PipelineObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        if (obj->type == PO_REPL) {
                            newaction->repl.replobj = obj;
                        } else {
                            THROW("state of replicate action must be a Replicate object");
                        }
                    } else {
                        THROW("pipeline or object '%s' not found", token);
                    }
                } else {
                    THROW("pipeline or object '%s' not found", token);
                }
            } else {
                if (stst->depth > MAX_DEPTH) {
                    THROW("jump depth exceeded");
                }
                pstring = strdup(pstring);
                struct StageState pstst = *stst;
                char *replname = strdup(token);
                pstst.stream = replname;
                pstst.headers = copy_header_list(stst->headers);
                pstst.actions = NULL;
                pstst.must_write = copy_mustwrite_list(stst, pstst.headers);
                pstst.depth += 1;
                if (stst->must_write && !pstst.must_write) {
                    //TODO we need a stst destructor
                    free(pstring);
                    free(replname);
                    delete_header_list(pstst.headers);
                    delete_must_write_list(pstst.must_write);
                    THROW("failed to copy the must_write list?!?");
                }
                if (!foreach_stages(pstring, process_stage, &pstst)) {
                    free(pstring);
                    free(replname);
                    delete_header_list(pstst.headers);
                    delete_confaction_list(pstst.actions);
                    delete_must_write_list(pstst.must_write);
                    THROW("failed to process pipeline '%s'", token);
                }
                free(pstring);
                free(replname);
                delete_must_write_list(pstst.must_write);
                delete_header_list(pstst.headers);
                if (pstst.actions == NULL) {
                    THROW("no actions in pipeline '%s'", token);
                }
                REVERSE_LIST(pstst.actions);
                replicatelist_push(&newaction->repl.pipelines, strdup(token), pstst.actions);
            }
            break; }
        case CA_SEND:
            if (newaction->send.iface) {
                THROW("we can only send on one interface at once");
            } else {
                struct Interface *iface = hashmap_find(stst->ifaces, token);
                if (iface == NULL) {
                    THROW("unknown interface '%s'", token);
                }
                //TODO dynconf: defer finalizing the iface pointer to assemble_actions() ?
                //      that's the least of our problems when dynconf changes interfaces :(
                newaction->send.iface = iface;
            }
            break;
        case CA_SEQGEN:
            if (newaction->seq.gen == NULL) {
                struct PipelineObject *obj = hashmap_find(stst->objects, token);
                if (obj) {
                    if (obj->type == PO_SEQGEN) {
                        newaction->seq.gen = obj;
                    } else {
                        THROW("seqgen argument must be a sequence generator object");
                    }
                } else {
                    THROW("seqgen argument must be a sequence generator object");
                }
            } else {
                THROW("seqgen only takes one argument");
            }
            break;
        case CA_TTLCHECK:
            THROW("ttlcheck action doesn't take parameters");
            break;
        case CA_TTLREDUCE:
            if (newaction->ttl.field == NULL) {
                struct HeaderDescriptor *hdr = header_list_find_by_name(stst->headers, token);
                if (hdr) {
                    if (header_list_find_by_name(hdr->next, token)) {
                        THROW("header name '%s' is ambiguous", token);
                    }
                    struct HeaderField *field = header_get_field_of_type(hdr,
                            header_index(stst->headers, hdr), FT_TTL);
                    if (field == NULL) {
                        THROW("header '%s' doesn't have a TTL field", hdr->name);
                    }
                    newaction->ttl.field = field;
                } else {
                    THROW("invalid header '%s'", token);
                }
            } else {
                THROW("this action only takes one argument");
            }
            break;
    }

    return true;
#undef THROW
}

// set nexthdr value in @dstheader based on the type of @typeheader
static struct ConfAssignment *assign_nexthdrid_from_header_type(const char *action_name,
        const struct HeaderDescriptor *dstheader, unsigned dstpos,
        const struct HeaderDescriptor *typeheader)
{
    uint16_t nexthdrnum;
    const struct Protocol *dstpr = &protocol_list[dstheader->id];
    unsigned dst_idx = dstpr->nexthdr_idx;
    const struct ProtocolField *dstf = &dstpr->header_fields[dst_idx];
    if (!dstpr->get_nexthdr(&nexthdrnum, typeheader->id)) {
        return NULL;
    }

    nexthdrnum = ntohs(nexthdrnum); // we need it in host order
    struct ConfAssignment *a = calloc_struct(ConfAssignment);
    a->text = strdup_printf("%s sets %s.%s=0x%.4x", action_name,
            dstheader->name, dstf->name, nexthdrnum);
    init_confvariable(&a->lhs, CVT_FIELD, dstf);
    init_confvariable(&a->rhs, CVT_CONST, dstf);
    prepare_constant_number(&a->rhs.value, nexthdrnum);

    struct HeaderField *dsthf = new_headerfield(dstpos, dstf);
    a->lhs.v.header.field = dsthf;
    a->write = header_get_field_writer(dsthf, &a->rhs.value);
    if (a->write == NULL) {
        free(dsthf);
        free(a);
        return NULL;
    }

    return a;
}

// copy nexthdr value from @srcheader to @dstheader if they are compatible
static struct ConfAssignment *assign_nexthdrid_copy_from_srcheader(const char *action_name,
        const struct HeaderDescriptor *dstheader, unsigned dstpos,
        const struct HeaderDescriptor *srcheader, unsigned srcpos)
{
    const struct Protocol *dstpr = &protocol_list[dstheader->id];
    unsigned dst_idx = dstpr->nexthdr_idx;
    const struct ProtocolField *dstf = &dstpr->header_fields[dst_idx];
    const struct Protocol *srcpr = &protocol_list[srcheader->id];
    unsigned src_idx = srcpr->nexthdr_idx;
    const struct ProtocolField *srcf = &srcpr->header_fields[src_idx];
    if (dstpr->get_nexthdr != srcpr->get_nexthdr) {
        log_error("nexthdr field type mismatch");
        return NULL;
    }

    struct ConfAssignment *a = calloc_struct(ConfAssignment);
    a->text = strdup_printf("%s sets %s.%s=%s.%s", action_name,
            dstheader->name, dstf->name, srcheader->name, srcf->name);
    init_confvariable(&a->lhs, CVT_FIELD, dstf);
    init_confvariable(&a->rhs, CVT_FIELD, srcf);

    struct HeaderField *srchf = new_headerfield(srcpos, srcf);
    a->rhs.v.header.field = srchf;
    a->read = header_get_field_reader(&a->lhs.value, srchf);
    if (a->read == NULL) {
        free(srchf);
        free(a);
        return NULL;
    }

    struct HeaderField *dsthf = new_headerfield(dstpos, dstf);
    a->lhs.v.header.field = dsthf;
    a->write = header_get_field_writer(dsthf, &a->rhs.value);
    if (a->write == NULL) {
        free(srchf);
        free(dsthf);
        free(a);
        return NULL;
    }

    return a;
}

static struct ConfAction *new_blank_confaction(struct StageState *stst, char *text)
{
    struct ConfAction *ret = calloc_struct(ConfAction);
    ret->text = text;
    ret->next = stst->actions;
    stst->actions = ret;
    return ret;
}

static struct ConfAction *new_confaction(struct StageState *stst, enum ConfActionType type, char *text)
{
    struct ConfAction *ret = new_blank_confaction(stst, text);
    ret->type = type;
    return ret;
}

static bool check_header_stack(const struct HeaderDescriptor *headers,
        enum ProtocolID *expected, unsigned count)
{
    for (unsigned i=0; i<count; i++) {
        if (headers == NULL) {
            log_warning("header %u should be %s", i,
                    protocol_type_from_id(expected[i]));
            return false;
        }
        if (headers->id != expected[i]) {
            log_warning("header %u is %s, expected %s", i,
                    protocol_type_from_id(headers->id), protocol_type_from_id(expected[i]));
            return false;
        }
        headers = headers->next;
    }
    return true;
}


static bool oam_mip_autoconfig(struct StageState *stst);

// here we do processing that needs all the parameters of the action
static bool process_action(struct StageState *stst)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("stream '%s' action '%s': " msg,                  \
                stst->stream, newaction->text, ##__VA_ARGS__);      \
        return false;                                               \
    } while (0)

    struct ConfAction *newaction = stst->actions;

    switch (newaction->type) {
        case CA_UNDEF:
            THROW("no action");
        case CA_ADD:
            log_debug("CA_ADD: %s %s %s %s",
                    newaction->add.beforeafter==ADD_BEFORE?"before":"after",
                    newaction->add.pos->name,
                    newaction->add.newname,
                    protocol_type_from_id(newaction->add.id));
            if (newaction->add.newname == NULL) {
                THROW("no new header name");
            }
            if (newaction->add.pos == NULL) {
                THROW("no existing header name");
            }
            if (newaction->add.beforeafter == ADD_UNKNOWN) {
                THROW("no header position specified");
            }

            struct HeaderDescriptor *newheader = calloc_struct(HeaderDescriptor);
            newheader->name = strdup(newaction->add.newname);
            newheader->id = newaction->add.id;

            // add newheader to stst->headers at the designated position
            // step 1: find the location in the linked list
            struct HeaderDescriptor *prevheader, *nextheader;
            if (newaction->add.beforeafter == ADD_BEFORE) {
                nextheader = newaction->add.pos;
                if (nextheader == stst->headers) {
                    prevheader = NULL;
                } else {
                    prevheader = stst->headers;
                    while (prevheader->next != newaction->add.pos)
                        prevheader = prevheader->next;
                }
            } else {
                prevheader = newaction->add.pos;
                nextheader = newaction->add.pos->next;
            }

            // step 2: get the header index and fix it in the assignments
            unsigned pos_idx = header_index(stst->headers, newaction->add.pos);
            if (newaction->add.beforeafter == ADD_AFTER) pos_idx++;
            newaction->add.pos_idx = pos_idx;
            for (struct ConfAssignment *a=newaction->add.assignments; a; a=a->next) {
                a->lhs.v.header.field->header_idx = pos_idx;
            }

            // step 3: actually add the header to the list
            newheader->next = nextheader;
            if (prevheader) {
                prevheader->next = newheader;
            } else {
                stst->headers = newheader;
            }

            // set sequence number if the new header has such a field
            const struct ProtocolField *seq_field = protocol_get_field_by_type(newheader->id, FT_TSNSEQ);
            if (seq_field) {
                if (stst->seq_set) {
                    struct ConfAction *writeseq = new_confaction(stst, CA_WRITESEQ,
                            strdup_printf("seq for %s", newaction->text));
                    writeseq->meta.field = new_headerfield(pos_idx, seq_field);
                    if (!process_action(stst)) // now writeseq is the newest action
                        return false;
                } else {
                    THROW("can't add %s header with undefined sequence number", protocol_type_from_id(newheader->id));
                }
            }

            // set timestamp if the new header has such a field
            const struct ProtocolField *tstamp_field = protocol_get_field_by_type(newheader->id, FT_TSNTSTAMP);
            if (tstamp_field) {
                struct ConfAction *writets = new_confaction(stst, CA_WRITETSTAMP,
                        strdup_printf("tstamp for %s", newaction->text));
                writets->meta.field = new_headerfield(pos_idx, tstamp_field);
                if (!process_action(stst)) // now writets is the newest action
                    return false;
            }

            // create a new EDIT action from the nexthdr setters and the header field assignments
            struct ConfAction *edit = new_confaction(stst, CA_EDIT,
                    strdup_printf("assignments for %s", newaction->text));

            // set the nexthdr field of newheader either by nextheader's type or by copying from prevheader
            if (protocol_list[newheader->id].get_nexthdr != NULL) {
                struct ConfAssignment *a = NULL;
                if (nextheader) {
                    a = assign_nexthdrid_from_header_type("add", newheader, pos_idx, nextheader);
                    if (a == NULL) {
                        THROW("header type %s cannot have type %s as next header",
                                protocol_type_from_id(newheader->id),
                                protocol_type_from_id(nextheader->id));
                    }
                } else {
                    if (!prevheader) {
                        THROW("need to set nexthdr but no information from next or previous header");
                    }
                    a = assign_nexthdrid_copy_from_srcheader("add", newheader, pos_idx, prevheader, pos_idx-1);
                    if (a == NULL) {
                        THROW("can't copy nexthdr type from previous header type %s",
                                protocol_type_from_id(prevheader->id));
                    }
                }
                a->next = edit->edit.assignments;
                edit->edit.assignments = a;
            }

            // set nexthdr of prevheader with newheader's type
            if (prevheader && protocol_list[prevheader->id].get_nexthdr != NULL) {
                struct ConfAssignment *a = assign_nexthdrid_from_header_type("add", prevheader, pos_idx-1, newheader);
                if (a == NULL) {
                    THROW("header type %s cannot have type %s as next header",
                            protocol_type_from_id(prevheader->id),
                            protocol_type_from_id(newheader->id));
                }
                a->next = edit->edit.assignments;
                edit->edit.assignments = a;
            }

            // add the assignments to the edit list
            if (newaction->add.assignments) {
                struct ConfAssignment *i = newaction->add.assignments;
                while (i->next) i = i->next;
                i->next = edit->edit.assignments;
                edit->edit.assignments = newaction->add.assignments;
                newaction->add.assignments = NULL;
            }

            if (edit->edit.assignments == NULL) {
                stst->actions = edit->next;
                free(edit->text);
                free(edit);
            } else {
                if (!process_action(stst)) // now edit is the newest action
                    return false;
            }
            break;
        case CA_DEL:
            if (newaction->del.hdr == NULL) {
                THROW("no header to delete");
            }
            struct HeaderDescriptor *del = newaction->del.hdr;
            struct HeaderDescriptor *prev = NULL;
            unsigned idx = 0;
            if (del != stst->headers) {
                idx++;
                prev = stst->headers;
                while (prev->next != del) {
                    idx++;
                    prev = prev->next;
                }
            }
            newaction->del.idx = idx;

            // if removing a sequence number tag (= end of tunnel), automatically filter OAM packets
            const struct ProtocolField *dseq_field = protocol_get_field_by_type(del->id, FT_TSNSEQ);
            if (dseq_field) {
                struct ConfAction *filter = new_confaction(stst, CA_FILTEROAM,
                        strdup_printf("filter before %s", del->name));
                filter->filteroam.field = new_headerfield(idx, dseq_field);
                if (!process_action(stst)) // now filter is the newest action
                    return false;

                // swap filter and delete so we are filtering before deleting
                stst->actions = confaction_swap_with_next(stst->actions);
            }

            // cancel TTL check if we've removed the header to be checked
            if (del == stst->needs_ttlcheck) {
                stst->needs_ttlcheck = NULL;
                // also cancel the TTLReduce action? no, OAM might need it
            }

            for (struct MustWriteField *mw=stst->must_write; mw; mw=mw->next) {
                if (mw->header == del) {
                    THROW("deleting header that is on the must_write list (field %s)",
                            mw->field->name);
                }
            }

            // handle the nexthdr field of prev
            struct ConfAssignment *a = NULL;
            if (prev) {
                if (protocol_list[prev->id].get_nexthdr) {
                    if (del->next) {
                        a = assign_nexthdrid_from_header_type("del", prev, idx-1, del->next);
                        if (a == NULL) {
                            THROW("header type %s cannot have type %s as next header",
                                    protocol_type_from_id(prev->id),
                                    protocol_type_from_id(del->next->id));
                        }
                    } else {
                        a = assign_nexthdrid_copy_from_srcheader("del", prev, idx-1, del, idx);
                        if (a == NULL) {
                            THROW("can't copy nexthdr type from deleted to previous header");
                        }
                    }
                }

                prev->next = del->next;
            } else {
                stst->headers = del->next;
            }
            del->next = NULL;
            delete_header_list(del);

            if (a) {
                struct ConfAction *dedit = new_confaction(stst, CA_EDIT,
                        strdup_printf("set nexthdr for %s", newaction->text));
                dedit->edit.assignments = a;
                if (!process_action(stst)) // now dedit is the newest action
                    return false;

                // swap dedit and delete so we are editing before deleting
                stst->actions = confaction_swap_with_next(stst->actions);
            }
            break;
        case CA_DELAY:
            if (stst->actions->delay.delay_value <= 0)
                THROW("delay parameter should be positive");
            if (stst->actions->d.delay.delay_value >= 2000)
                THROW("delay parameter should not more than 2 seconds.");

            if (stst->actions->delay.offload)
                stst->had_delay_offload = true;
            else if (!stst->actions->delay.offload && stst->had_delay_offload)
                THROW("after delay offload set, any delay without offload is unacceptable");
            break;
        case CA_DROP:
            stst->had_final = true;
            break;
        case CA_EDIT:
            log_debug("CA_EDIT: %s", newaction->text);
            if (newaction->edit.assignments != NULL) {
                REVERSE_LIST(newaction->edit.assignments);
            } else {
                THROW("no assignments in edit");
            }
            break;
        case CA_ELIM:
            if (newaction->elim.rec == NULL) {
                THROW("eliminate needs a sequence recovery object");
            }
            if (newaction->elim.pipename == NULL) {
                THROW("eliminate needs an action pipeline (since version 6.3)");
            }
            if (!stst->seq_set) {
                THROW("can't eliminate without a sequence number");
            }
            if (newaction->elim.rec->auto_mip_level > 0 && !oam_mip_autoconfig(stst))
                log_warning("cannot generate MIP for '%s'", newaction->elim.rec->name);
            struct ConfAction *elimjump = new_confaction(stst, CA_JUMP,
                    strdup_printf("auto-generated jump for %s", newaction->text));
            elimjump->jump.pipename = strdup(newaction->elim.pipename);
            if (!process_action(stst))
                THROW("can't jump to '%s' after elimination", elimjump->jump.pipename);
            break;
        case CA_FILTEROAM:
            // the user can't create this action, so nothing to verify here
            break;
        case CA_JUMP:
            log_debug("CA_JUMP: %s", newaction->text);
            if (newaction->jump.pipename == NULL) {
                THROW("no action pipeline to jump to");
            }
            char *pipestring = inisection_get(stst->streams_sec, newaction->jump.pipename);
            if (pipestring) {
                if (stst->depth > MAX_DEPTH) {
                    THROW("jump depth exceeded");
                }

                pipestring = strdup(pipestring);
                struct StageState jstst = *stst;
                char *jumpname = strdup(newaction->jump.pipename);
                jstst.stream = jumpname;
                jstst.actions = NULL;
                jstst.depth += 1;
                // not copying header and must_write lists, because we don't branch (unlike REPLICATE)

                if (!foreach_stages(pipestring, process_stage, &jstst)) {
                    free(pipestring);
                    free(jumpname);
                    delete_confaction_list(jstst.actions);
                    THROW("failed to process pipeline '%s'", newaction->jump.pipename);
                }
                free(pipestring);
                free(jumpname);
                if (jstst.actions == NULL) {
                    THROW("no actions in pipeline '%s'", newaction->jump.pipename);
                }

                // replace jump with the newly read action list
                struct ConfAction *jump = newaction;
                stst->actions = confaction_concat(jstst.actions, jump->next);
                jump->next = NULL;
                delete_confaction_list(jump);

                stst->headers = jstst.headers;
                // normally this list is empty, but get it back from jstst anyway
                stst->must_write = jstst.must_write;
                stst->had_final = true;
            } else {
                THROW("action pipeline '%s' not found", newaction->jump.pipename);
            }
            break;
        case CA_MEPSTART:
        case CA_MEPSTOP:
        case CA_MIP:
            if (newaction->oam.name == NULL) {
                THROW("unnamed OAM action (name is mandatory)");
            }
            if (newaction->oam.level == -1) {
                THROW("no level specified for '%s' OAM action", newaction->oam.name);
            }
            enum ProtocolID expected[] = {PROTO_ID_MPLS, PROTO_ID_DCW};
            if (check_header_stack(stst->headers, expected, 2) == false) {
                if (newaction->oam.auto_generated == false)
                    THROW("header stack is not suitable for OAM point");
                else {
                    // for auto-generated MIPs it is just a warning
                    log_warning("stream '%s' action '%s': header stack is not suitable for OAM point",
                                stst->stream, newaction->text);
                    return false;
                }
            }
            //TODO remove CA_MIP for now, it should be there but causes trouble
            if (newaction->type == CA_MEPSTART /*|| newaction->type == CA_MIP*/) {
                // this is a packet injection point, user must write label before sending
                struct MustWriteField *mw = calloc_struct(MustWriteField);
                mw->header = stst->headers;
                mw->field = protocol_get_field_by_name(PROTO_ID_MPLS, "label");
                mw->next = stst->must_write;
                stst->must_write = mw;
            }
            break;
        case CA_POF:
            if (newaction->pof.pof == NULL) {
                THROW("no POF object specified");
            }
            if (!stst->seq_set) {
                THROW("can't have POF with undefined sequence number");
            }
            break;
        case CA_READSEQ:
        case CA_WRITESEQ:
            if (newaction->meta.field == NULL) {
                THROW("no header specified");
            }
            if (newaction->type == CA_READSEQ) {
                stst->seq_set = true;
            } else {
                if (!stst->seq_set) {
                    THROW("can't write undefined sequence number");
                }
            }
            break;
        case CA_READTSTAMP:
        case CA_WRITETSTAMP:
            if (newaction->meta.field == NULL) {
                THROW("no header specified");
            }
            break;
        case CA_REPL:
            log_debug("CA_REPL: %s", newaction->text);
            if (newaction->repl.pipelines == NULL) {
                THROW("no pipelines specified");
            }
            if (newaction->repl.replobj && newaction->repl.replobj->auto_mip_level > 0 && !oam_mip_autoconfig(stst))
                log_warning("cannot generate MIP for '%s'", newaction->repl.replobj->name);
            REVERSE_LIST(newaction->repl.pipelines);
            stst->had_final = true;
            break;
        case CA_SEND:
            if (newaction->send.iface == NULL) {
                THROW("no send interface specified");
            }
            if (stst->needs_ttlcheck) {
                struct ConfAction *ttlcheck = new_confaction(stst, CA_TTLCHECK, strdup("auto-check before send"));
                (void)ttlcheck;
                if (!process_action(stst)) // now ttlcheck is the newest action
                    return false;

                // swap them so we check before send
                stst->actions = confaction_swap_with_next(stst->actions);
                stst->needs_ttlcheck = 0;
            }
            if (stst->must_write) {
                for (struct MustWriteField *mw=stst->must_write; mw; mw=mw->next) {
                    log_error("stream %s must write field %s:%s before sending",
                            stst->stream, mw->header->name, mw->field->name);
                }
                return false;
            }
            break;
        case CA_SEQGEN:
            if (newaction->seq.gen == NULL) {
                THROW("seqgen needs a sequence generator object");
            }
            stst->seq_set = true;
            break;
        case CA_TTLCHECK:
            if (stst->ttl_set == false) {
                THROW("can't check undefined TTL, need TTLReduce first");
            }
            break;
        case CA_TTLREDUCE:
            stst->ttl_set = true;
            break;
    }
    return true;
#undef THROW
}

static bool process_stage(char *stage, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->had_final) {
        log_error("can't have more actions after %s",
                confaction_name_from_type(stst->actions->type));
        return false;
    }

    struct ConfAction *newaction = new_blank_confaction(stst, strdup(stage));

    if (!foreach_tokens(stage, process_token, stst)) {
        log_error("failed to process action parameters '%s'", newaction->text);
        return false;
    }

    if (stst->actions->type == CA_UNDEF) {
        log_error("no action in '%s'", newaction->text);
        return false;
    }

    // now we have all arguments for the action, let's process it properly
    if (!process_action(stst)) {
        log_error("failed to process action '%s'", newaction->text);
        return false;
    }

    return true;
}

struct ConfAction *parse_actions_line(const char *stream, const char *line,
        const struct HeaderDescriptor *headers,
        const struct HashMap *ifaces,
        const struct HashMap *objects,
        const struct IniSection *streams_sec)
{
    struct StageState stst = {
        .stream = stream,
        .actions = NULL,
        .headers = copy_header_list(headers),
        .ifaces = ifaces,
        .objects = objects,
        .streams_sec = streams_sec,
        .had_final = false,
        .seq_set = false,
        .ttl_set = false,
        .needs_ttlcheck = NULL,
        .must_write = NULL,
        .depth = 0,
    };

    // automatically reduce TTL & schedule a check, if the very first header has such a field
    const struct ProtocolField *ttlfield = protocol_get_field_by_type(headers->id, FT_TTL);
    if (ttlfield) {
        new_confaction(&stst, CA_TTLREDUCE, strdup_printf("automatic TTL reduce on %s", headers->name));
        struct HeaderField *field = header_get_field_of_type(stst.headers, 0, FT_TTL);
        stst.actions->ttl.field = field;
        stst.ttl_set = true;
        stst.needs_ttlcheck = stst.headers;
    }

    char *aline = strdup(line);
    if (!foreach_stages(aline, process_stage, &stst)) {
        log_error("failed to process actions line for stream '%s'", stream);
        free(aline);
        delete_header_list(stst.headers);
        delete_confaction_list(stst.actions);
        return NULL;
    }
    free(aline);

    if (stst.actions == NULL) {
        log_error("no actions in actions line for stream '%s'", stream);
        delete_header_list(stst.headers);
        return NULL;
    }

    REVERSE_LIST(stst.actions);

    //TODO perform optimization passes on the action list
    //      e.g. merge subsequent Edit actions

    //TODO in stst.actions set all pointers to stst.headers to NULL

    delete_header_list(stst.headers);

    if (stst.must_write) {
        // strange edge case...
        log_warning("there was a MEP-START, no EDIT label, no SEND");
        delete_must_write_list(stst.must_write);
    }

    return stst.actions;
}

static void delete_confassignments(struct ConfAssignment *assignments)
{
    while (assignments) {
        struct ConfAssignment *del = assignments;
        assignments = assignments->next;
        free(del->text);
        free(del->rhs.value.value);
        if (del->lhs.type == CVT_FIELD)
            free(del->lhs.v.header.field);
        if (del->rhs.type == CVT_FIELD)
            free(del->rhs.v.header.field);
        if (del->rhs.type == CVT_IFACE)
            free(del->rhs.v.iface.property);
        free(del);
    }
}

static void delete_replicatelist(struct ReplicateList *pipelines)
{
    while (pipelines) {
        struct ReplicateList *del = pipelines;
        pipelines = pipelines->next;
        free(del->name);
        delete_confaction_list(del->actions);
        free(del);
    }
}

struct ConfAction *delete_confaction_list(struct ConfAction *ca_list)
{
    while (ca_list) {
        struct ConfAction *del = ca_list;
        ca_list = ca_list->next;
        free(del->text);
        switch (del->type) {
            case CA_UNDEF:
                break;
            case CA_ADD:
                free(del->add.newname);
                delete_confassignments(del->add.assignments);
                break;
            case CA_DEL:
                break;
            case CA_DELAY:
                break;
            case CA_DROP:
                break;
            case CA_EDIT:
                delete_confassignments(del->edit.assignments);
                break;
            case CA_ELIM:
                free(del->elim.pipename);
                break;
            case CA_FILTEROAM:
                free(del->filteroam.field);
                break;
            case CA_JUMP:
                free(del->jump.pipename);
                break;
            case CA_POF:
                break;
            case CA_READSEQ:
            case CA_READTSTAMP:
            case CA_WRITESEQ:
            case CA_WRITETSTAMP:
                free(del->meta.field);
                break;
            case CA_REPL:
                delete_replicatelist(del->repl.pipelines);
                break;
            case CA_SEND:
                break;
            case CA_SEQGEN:
                break;
            case CA_TTLCHECK:
                break;
            case CA_TTLREDUCE:
                free(del->ttl.field);
                break;
            case CA_MEPSTART:
            case CA_MEPSTOP:
            case CA_MIP:
                free(del->oam.name);
                free(del->oam.stream);
                break;
        }
        free(del);
    }

    return NULL;
}

static struct EditAssign *assemble_fieldassigns(struct ConfAssignment *list, unsigned *assigncount)
{
    unsigned count = 0;
    for (struct ConfAssignment *l=list; l; l=l->next) count++;
    if (count == 0) {
        *assigncount = 0;
        return NULL;
    }
    struct EditAssign *ret = calloc_struct_array(EditAssign, count);

    unsigned i=0;
    for (struct ConfAssignment *l=list; l; l=l->next) {
        struct EditAssign *a = ret+i;
        a->text = strdup(l->text);
        a->write = l->write;
        a->read = l->read;
        if (l->lhs.type == CVT_UNDEF) {
            log_error("assign '%s' destination is undefined", l->text);
            for (unsigned j=0; j<=i; j++) free(ret[j].text);
            free(ret);
            return NULL;
        }
        if (l->lhs.type == CVT_FIELD)
            a->write_state = memdup(l->lhs.v.header.field, sizeof(struct HeaderField));

        switch (l->rhs.type) {
            case CVT_UNDEF:
                log_error("assign '%s' source is undefined", l->text);
                for (unsigned j=0; j<=i; j++) free(ret[j].text);
                free(ret);
                return NULL;
            case CVT_FIELD:
                a->read_state = memdup(l->rhs.v.header.field, sizeof(struct HeaderField));
                a->owns_read_state = true;
                break;
            case CVT_CONST:
                a->constant = l->rhs.value;
                unsigned len = DIVCEIL(a->constant.bitoffset + a->constant.bitcount, 8);
                a->constant.value = memdup(l->rhs.value.value, len);
                break;
            case CVT_IFACE:
                a->read_state = l->rhs.v.iface.iface;
                a->owns_read_state = false;
                break;
        }

        i += 1;
    }

    *assigncount = count;
    return ret;
}

// add reference to the outgoing interfaces so they know they are in use
static void ref_send_interfaces(struct Pipeline *pipe)
{
    for (unsigned i=0; i<pipe->action_count; i++) {
        if (pipe->actions[i].type == ACT_SEND) {
            iface_ref(action_send_get_iface(pipe->actions+i));
        }
    }
}

struct Pipeline *assemble_actions(const char *stream_name, const struct ConfAction *ca_list)
{
#define THROW(msg, ...)                                         \
    do {                                                        \
        log_error("assemble actions stream %s action %u: " msg, \
                stream_name, i, ##__VA_ARGS__);                 \
        free(actions);                                          \
        free(ret);                                              \
        return NULL;                                            \
    } while (0)

    unsigned count = 0;
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next)
        if (ca->type != CA_MEPSTART) // do not include MEPStart into the action list
            count++;

    log_info("assemble actions %s count %u", stream_name, count);

    if (count == 0) {
        return NULL;
    }

    struct Pipeline *ret = calloc_struct(Pipeline);

    struct Action *actions = calloc_struct_array(Action, count);

    unsigned i = 0;
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) {
        switch (ca->type) {
            case CA_UNDEF:
                log_error("cannot assemble undefined action");
                break;
            case CA_ADD:
                create_action_add(actions+i, ca->add.pos_idx, ca->add.id, ca->add.len, ca->text);
                break;
            case CA_DEL:
                create_action_del(actions+i, ca->del.idx, ca->text);
                break;
            case CA_DELAY:
                struct timespec delay;
                timespec_from_usec(&delay, (unsigned)(ca->delay.delay_value * 1000)); // convert float type millisec to timespec
                create_action_delay(ret+a, delay, ca->delay.offload, ca->text);
                break;
            case CA_DROP:
                create_action_drop(actions+i, ca->text);
                break;
            case CA_EDIT: {
                unsigned acount;
                struct EditAssign *assigns = assemble_fieldassigns(ca->edit.assignments, &acount);
                if (assigns == NULL) {
                    THROW("could not assemble the field assignments for edit action");
                }
                create_action_edit(actions+i, assigns, acount, ca->text);
                break; }
            case CA_ELIM:
                create_action_elim(actions+i, ca->elim.rec, ca->text);
                break;
            case CA_FILTEROAM:
                create_action_filteroam(actions+i, ca->filteroam.field, ca->text);
                break;
            case CA_JUMP:
                THROW("jump should have been inlined");
            case CA_MEPSTART: {
                if (!oam_create_mep_start(ca->oam.stream, ca->oam.name, ca->oam.level, ret, i)) {
                    THROW("couldn't create MEP Start");
                }
                break; }
            case CA_MEPSTOP:
                create_action_mepstop(actions+i, ca->oam.stream, ca->oam.level, ca->oam.obj, ca->oam.name, ca->text);
                break;
            case CA_MIP:
                create_action_mip(actions+i, ca->oam.stream, ca->oam.level, ca->oam.obj, ca->oam.name, ca->text);
                if (!oam_create_mep_start(ca->oam.stream, ca->oam.name, ca->oam.level, ret, i+1)) {
                    THROW("couldn't create start point for MIP");
                }
                break;
            case CA_POF:
                create_action_pof(actions+i, ca->pof.pof, ca->text);
                break;
            case CA_READSEQ:
                create_action_readseq(actions+i, ca->meta.field, ca->text);
                break;
            case CA_READTSTAMP:
                create_action_readtstamp(actions+i, ca->meta.field, ca->text);
                break;
            case CA_REPL: {
                struct PipelineList *pipes = NULL;
                for (struct ReplicateList *r=ca->repl.pipelines; r; r=r->next) {
                    struct Pipeline *r_pipe = assemble_actions(r->name, r->actions);
                    if (r_pipe == NULL) {
                        log_error("failed to assemble actions for branch %s of replicate in stream %s",
                                r->name, stream_name);
                        //TODO cleanup on error
                        return NULL;
                    }
                    pipeline_ref(r_pipe);

                    struct PipelineList *p = calloc_struct(PipelineList);
                    p->pipe = r_pipe;
                    p->text = r->name;
                    p->next = pipes;
                    pipes = p;
                }
                REVERSE_LIST(pipes);
                create_action_repl(actions+i, pipes, ca->repl.replobj, ca->text);
                break; }
            case CA_SEND:
                create_action_send(actions+i, ca->send.iface, ca->text);
                break;
            case CA_SEQGEN:
                create_action_seqgen(actions+i, ca->seq.gen, ca->text);
                break;
            case CA_TTLCHECK:
                create_action_ttlcheck(actions+i, ca->text);
                break;
            case CA_TTLREDUCE:
                create_action_ttlreduce(actions+i, ca->ttl.field, ca->text);
                break;
            case CA_WRITESEQ:
                create_action_writeseq(actions+i, ca->meta.field, ca->text);
                break;
            case CA_WRITETSTAMP:
                create_action_writetstamp(actions+i, ca->meta.field, ca->text);
                break;
        }
        if (ca->type != CA_MEPSTART) // this doesn't appear as an action
            i++;
    }

    ret->actions = actions;
    ret->action_count = count;
    ret->name = strdup(stream_name);
    ref_send_interfaces(ret);
    return ret;
#undef THROW
}

void confactions_print(const struct ConfAction *ca_list, unsigned indent)
{
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) {
        log_info("%*cConfAction %s '%s'", indent, ' ',
                confaction_name_from_type(ca->type), ca->text);
        switch (ca->type) {
            case CA_UNDEF:
                break;
            case CA_ADD:
                log_debug("%*cnew name %s id %d type %s len %u index %u", indent+2, ' ',
                        ca->add.newname, ca->add.id,
                        protocol_type_from_id(ca->add.id),
                        ca->add.len, ca->add.pos_idx);
                break;
            case CA_DEL:
                log_debug("%*cindex %u", indent+2, ' ', ca->del.idx);
                break;
            case CA_DELAY:
                log_debug("%*cdelaying %f ms", indent+2, ' ', ca->delay.delay_value);
                break;
            case CA_DROP:
                break;
            case CA_EDIT:
                for (struct ConfAssignment *a=ca->edit.assignments; a; a=a->next) {
                    log_debug("%*c%s", indent+2, ' ', a->text);
                    log_debug("%*cread %p write %p", indent+3, ' ', a->read, a->write);
                    log_debug("%*clhs type %s valuetype %s bitoffset %u bitcount %u", indent+3, ' ',
                            variabletype_name_from_type(a->lhs.type),
                            fieldtype_name_from_type(a->lhs.value_type),
                            a->lhs.value.bitoffset, a->lhs.value.bitcount);
                    if (a->lhs.type == CVT_FIELD) {
                        log_debug("%*cindex %u", indent+4, ' ', a->lhs.v.header.field->header_idx);
                    }
                    log_debug("%*crhs type %s valuetype %s bitoffset %u bitcount %u", indent+3, ' ',
                            variabletype_name_from_type(a->rhs.type),
                            fieldtype_name_from_type(a->rhs.value_type),
                            a->rhs.value.bitoffset, a->rhs.value.bitcount);
                    if (a->rhs.type == CVT_FIELD) {
                        log_debug("%*cindex %u", indent+4, ' ', a->rhs.v.header.field->header_idx);
                    } else if (a->rhs.type == CVT_CONST) {
                        unsigned bytes = DIVCEIL(a->rhs.value.bitoffset + a->rhs.value.bitcount, 8);
                        unsigned char *cst = a->rhs.value.value;
                        char b_str[32];
                        unsigned b_off = 0;
                        if (log_enabled(INFO)) {
                            for (unsigned i=0; i<bytes; i++) {
                                b_off += snprintf(b_str+b_off, sizeof(b_str)-b_off, " 0x%.2x", cst[i]);
                            }
                        }
                        log_debug("%*cconstant:%s", indent+4, ' ', b_str);
                    }
                }
                break;
            case CA_ELIM:
                log_debug("%*cobject %s", indent+2, ' ', ca->elim.rec->name);
                break;
            case CA_FILTEROAM:
                log_debug("%*cfield idx %u bitoffset %u bitcount %u", indent+2, ' ',
                        ca->filteroam.field->header_idx, ca->filteroam.field->bitoffset,
                        ca->filteroam.field->bitcount);
                break;
            case CA_JUMP:
                log_debug("%*c%s", indent+2, ' ', ca->jump.pipename);
                break;
            case CA_POF:
                log_debug("%*cobject %s", indent+2, ' ', ca->pof.pof->name);
                break;
            case CA_READSEQ:
            case CA_READTSTAMP:
            case CA_WRITESEQ:
            case CA_WRITETSTAMP:
                log_debug("%*cfield idx %u bitoffset %u bitcount %u", indent+2, ' ',
                        ca->meta.field->header_idx, ca->meta.field->bitoffset, ca->meta.field->bitcount);
                break;
            case CA_REPL:
                for (struct ReplicateList *p=ca->repl.pipelines; p; p=p->next) {
                    log_info("%*cbranch %s", indent+2, ' ', p->name);
                    confactions_print(p->actions, indent+4);
                }
                break;
            case CA_SEND:
                log_debug("%*ciface %s", indent+2, ' ', ca->send.iface ? ca->send.iface->name : "UNKNOWN");
                break;
            case CA_SEQGEN:
                log_debug("%*cobject %s", indent+2, ' ', ca->seq.gen->name);
                break;
            case CA_TTLCHECK:
                log_debug("%*cfield idx %u bitoffset %u bitcount %u", indent+2, ' ',
                        ca->ttl.field->header_idx, ca->ttl.field->bitoffset, ca->ttl.field->bitcount);
                break;
            case CA_TTLREDUCE:
                log_debug("%*cfield idx %u bitoffset %u bitcount %u", indent+2, ' ',
                        ca->ttl.field->header_idx, ca->ttl.field->bitoffset, ca->ttl.field->bitcount);
                break;
            case CA_MEPSTART:
            case CA_MEPSTOP:
            case CA_MIP:
                log_debug("%*cname %s level %d object %s", indent+2, ' ',
                        ca->oam.name, ca->oam.level, ca->oam.obj?ca->oam.obj->name:"<none>");
                break;
        }
    }
}

static bool oam_mip_autoconfig(struct StageState *stst)
{
    struct ConfAction *ca = stst->actions;
    switch (ca->type) {
        case CA_ELIM: {
            char *pre = strdup_printf("o_%s_L%u_pre-%s", stst->stream,
                                      ca->elim.rec->auto_mip_level, ca->elim.rec->name);
            char *post = strdup_printf("o_%s_L%u_post-%s", ca->elim.pipename,
                                       ca->elim.rec->auto_mip_level, ca->elim.rec->name);
            struct ConfAction *ca_pre = new_confaction(stst, CA_MIP,
                           strdup_printf("auto-generated MIP before '%s'", ca->text));
            ca_pre->oam.name = pre;
            ca_pre->oam.level = ca->elim.rec->auto_mip_level;
            ca_pre->oam.obj = ca->elim.rec;
            ca_pre->oam.auto_generated = true;
            ca_pre->oam.stream = strdup(stst->stream);
            if (!process_action(stst)) {
                free(pre);
                free(ca_pre->oam.stream);
                free(ca_pre);
                stst->actions = ca;
                return false;
            }
            stst->actions = confaction_swap_with_next(stst->actions);
            struct ConfAction *ca_post = new_confaction(stst, CA_MIP,
                            strdup_printf("auto-generated MIP after '%s'", ca->text));
            ca_post->oam.name = post;
            ca_post->oam.level = ca->elim.rec->auto_mip_level;
            ca_post->oam.obj = ca->elim.rec;
            ca_post->oam.auto_generated = true;
            ca_post->oam.stream = strdup(ca->elim.pipename);
            if (!process_action(stst)) {
                free(pre);
                free(post);
                free(ca_pre->oam.stream);
                free(ca_post->oam.stream);
                free(ca_pre);
                free(ca_post);
                stst->actions = ca;
                return false;
            }
            break; }
        case CA_REPL: {
            char *pre = strdup_printf("o_%s_L%u_pre-%s", stst->stream,
                                      ca->repl.replobj->auto_mip_level, ca->repl.replobj->name);
            struct ConfAction *ca_pre = new_confaction(stst, CA_MIP,
                           strdup_printf("auto-generated MIP before '%s'", ca->text));
            ca_pre->oam.name = pre;
            ca_pre->oam.level = ca->repl.replobj->auto_mip_level;
            ca_pre->oam.obj = ca->repl.replobj;
            ca_pre->oam.auto_generated = true;
            ca_pre->oam.stream = strdup(stst->stream);
            if (!process_action(stst)) {
                free(pre);
                free(ca_pre->oam.stream);
                free(ca_pre);
                stst->actions = ca;
                return false;
            }
            break; }
        default:
            return false;
    }
    return true;
}
