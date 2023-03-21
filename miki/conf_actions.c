
#include "conf_actions.h"
#include "conf_object.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "action.h"
#include "header.h"
#include "interface.h"
#include "inifile.h"
#include "packet.h"
#include "parsetree.h"
#include "pipeline.h"
#include "protocol.h"
#include "seq_gen.h"
#include "transfer.h"
#include "utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <arpa/inet.h> /* ntohs() */

//TODO more actions: readseq, writeseq, readtstamp, writetstamp
enum ConfActionType {
    CA_ADD = 1,
    CA_DEL,
    CA_DELAY,
    CA_DROP,
    CA_EDIT,
    CA_ELIM,
    CA_JUMP,
    CA_POF,
    CA_REPL,
    CA_SEND,
};

enum BeforeAfter {
    ADD_UNKNOWN,
    ADD_BEFORE,
    ADD_AFTER,
};

enum ConfVariableType {
    CVT_UNDEF,
    CVT_FIELD, // header field
    CVT_PACKET, // packet metadata
    // the ones below are rhs-only
    CVT_CONST, // constant
    CVT_IFACE, // interface property
    CVT_GEN, // generator object
};

struct ConfVariable {
    enum ConfVariableType type;
    enum ProtocolFieldType value_type;
    struct Value value; // describes the passed value, and stores constant
    value_consumer *write;
    value_producer *read;
    union {
        struct {
            //struct HeaderDescriptor *header;
            struct HeaderField *field; // state for read/write
        } field;
        struct {
            char *name;
        } meta;
        struct {
            struct ConfObject *obj; // state for read
        } object;
        struct {
            struct Interface *iface; // state for read
            char *property;
        } iface;
    } v;
};

struct ConfAssignment {
    struct ConfVariable lhs;
    struct ConfVariable rhs;
    char *text;
    struct ConfAssignment *next;
};

struct ReplicateList {
    char *string;
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
            char *newtype;
            int id;
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
            struct HeaderField timestamp_field;
            struct ConfVariable delay_value;
        } delay;
        struct {
            struct ConfAssignment *assignments;
        } edit;
        struct {
            struct SequenceRecovery *rec;
            value_producer *seq_producer;
            void *seq_producer_state;
        } elim;
        struct {
            char *pipename;
        } jump;
        struct {
            struct ConfObject *pof; //TODO struct Pof
            //struct HeaderField seq_field;
        } pof;
        struct {
            struct ReplicateList *pipelines;
        } repl;
        struct {
            struct Interface *iface;
        } send;
    } d;
};

struct StageState {
    const char *stream;
    struct ConfAction *actions;
    struct HeaderDescriptor *headers;
    struct Interface *ifaces;
    unsigned ifcount;
    struct HashMap *objects;
    struct IniSection *streams_sec;
    bool had_final;
};


static void replicatelist_push_string(struct ReplicateList **list, char *string)
{
    struct ReplicateList *l = calloc_struct(ReplicateList);
    l->string = string;
    l->next = *list;
    *list = l;
}

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
        w->type = strdup(h->type);
        w->name = strdup(h->name);
        w->id = h->id;

        h = h->next;
    }

    return ret;
}

// @returns the position of @pos in the linked list @headers
// @pos must be in the linked list!
// skips the headers marked as CH_DEL
static unsigned header_index(const struct HeaderDescriptor *headers, const struct HeaderDescriptor *pos)
{
    unsigned pos_idx = 0;
    for (const struct HeaderDescriptor *h=headers; h!=pos; h=h->next) pos_idx++;
    return pos_idx;
}

static struct Interface *find_interface(struct StageState *stst, const char *name)
{
    for (unsigned i=0; i<stst->ifcount; i++) {
        if (strcmp(stst->ifaces[i].name, name) == 0)
            return stst->ifaces + i;
    }
    return NULL;
}

static const char *confaction_name_from_type(enum ConfActionType type)
{
    switch (type) {
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
        case CA_JUMP:
            return "Jump";
        case CA_POF:
            return "POF";
        case CA_REPL:
            return "Replicate";
        case CA_SEND:
            return "Send";
    }
    return NULL;
}

static const char *fieldtype_name_from_type(enum ProtocolFieldType type)
{
    switch (type) {
        case FT_UNKNOWN:
            return "Unknown";
        case FT_NUMBER:
            return "Number";
        case FT_MACADDRESS:
            return "MAC";
        case FT_IPV4ADDRESS:
            return "IPv4";
        case FT_IPV6ADDRESS:
            return "IPv6";
        case FT_TSNSEQ:
            return "TSNSeq";
        case FT_TSNTSTAMP:
            return "TSNTstamp";
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
        case CVT_PACKET:
            return "Metadata";
        case CVT_CONST:
            return "Constant";
        case CVT_IFACE:
            return "Interface";
        case CVT_GEN:
            return "Generator";
    }
    return NULL;
}

static void init_confvariable(struct ConfVariable *v,
        enum ConfVariableType type, enum ProtocolFieldType value_type,
        unsigned bitoffset,
        unsigned bitcount)
{
    v->type = type;
    v->value_type = value_type;
    v->value.bitoffset = bitoffset;
    v->value.bitcount = bitcount;
    v->value.value = NULL;
}

// @returns false on error
static bool process_assignment_lhs(struct HeaderDescriptor *headers, struct ConfVariable *lhs, char *string)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, msg "\n", ##__VA_ARGS__);                   \
        return false;                                               \
    } while (0)

    char *hdr, *field;
    if (parse_fieldname(string, &hdr, &field)) {
        if (strcmp(hdr, "meta") == 0) {
            init_confvariable(lhs, CVT_PACKET, packet_get_property_type(field), 0, 32);
            if (lhs->value_type == FT_UNKNOWN) {
                THROW("packet has no metadata named '%s'", field);
            }
            lhs->v.meta.name = strdup(field);
        } else {
            struct HeaderDescriptor *h = header_list_find_by_name(headers, hdr);
            if (h == NULL) {
                THROW("no header named '%s' in the packet", hdr);
            }
            if (header_list_find_by_name(h->next, hdr)) {
                THROW("header name '%s' is ambiguous", hdr);
            }
            struct ProtocolField *f = protocol_get_field_by_name(h->id, field);
            if (f == NULL) {
                THROW("header '%s' has no field named '%s'", hdr, field);
            }
            init_confvariable(lhs, CVT_FIELD, f->type, f->bitoffset, f->bitcount);
            struct HeaderField *hf = new_headerfield(header_index(headers, h), f->bitoffset, f->bitcount);
            //lhs->v.field.header = h;
            lhs->v.field.field = hf;
        }
    } else {
        THROW("left-hand-side '%s' of the assignment is invalid", string);
    }
    return true;
#undef THROW
}

// @returns false on error
static bool process_assignment_rhs(struct StageState *stst, const struct ConfVariable *lhs,
        struct ConfVariable *rhs, char *string)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, msg "\n", ##__VA_ARGS__);                   \
        return false;                                               \
    } while (0)

    printf("process_assignment_rhs '%s'\n", string);
    char *key, *val;
    if (parse_fieldname(string, &key, &val)) {
        printf("rhs has a dot '%s' . '%s'\n", key, val);
        struct HeaderDescriptor *h = header_list_find_by_name(stst->headers, key);
        if (h) {
            if (header_list_find_by_name(h->next, key)) {
                THROW("header name '%s' is ambiguous", key);
            }
            struct ProtocolField *f = protocol_get_field_by_name(h->id, val);
            if (f) {
                printf("rhs is a header field!\n");
                if (lhs->value_type != f->type) {
                    THROW("types of left-hand-side %s and right-hand-side %s don't match",
                            fieldtype_name_from_type(lhs->value_type), fieldtype_name_from_type(f->type));
                }
                init_confvariable(rhs, CVT_FIELD, f->type, f->bitoffset, f->bitcount);
                struct HeaderField *hf = new_headerfield(header_index(stst->headers, h),
                        f->bitoffset, f->bitcount);
                //rhs->v.field.header = h;
                rhs->v.field.field = hf;
                rhs->read = header_get_field_reader(&lhs->value, hf);
                if (rhs->read == NULL) {
                    //TODO can we print the name of lhs?
                    THROW("cannot read field %s.%s into the left-hand-side expression", key, val);
                }
                return true;
            } else {
                THROW("header %s has no field %s", key, val);
            }
        }

        struct Interface *iface = find_interface(stst, key);
        if (iface) {
            printf("rhs is an interface!\n");
            if (iface->get_property_reader) {
                rhs->read = iface->get_property_reader(iface, val, lhs->value_type, &lhs->value);
                if (rhs->read == NULL) {
                    THROW("interface %s has no property named '%s'", iface->name, val);
                }
            } else {
                THROW("interface %s has no queryable property", iface->name);
            }
            // note: bitoffset, bitcount were already set by get_property_reader()
            init_confvariable(rhs, CVT_IFACE, lhs->value_type, rhs->value.bitoffset, rhs->value.bitcount);
            rhs->v.iface.iface = iface;
            rhs->v.iface.property = strdup(val);
            return true;
        }

        if (strcmp(key, "meta") == 0) {
            printf("rhs is a packet metadata!\n");
            enum ProtocolFieldType rhstype = packet_get_property_type(val);
            if (rhstype == FT_UNKNOWN) {
                THROW("packet has no metadata named '%s'", val);
            }
            if (lhs->value_type != rhstype) {
                THROW("cannot read packet meta '%s' into the left-hand-side expression", val);
            }
            rhs->read = packet_get_property_reader(val, &lhs->value);
            if (rhs->read == NULL) {
                THROW("can't read metadata '%s' into the given target", val);
            }
            init_confvariable(rhs, CVT_PACKET, rhstype, 0, 32);
            rhs->v.meta.name = strdup(val);
            return true;
        }

        // if nothing matched, restore the dot
        val[-1] = '.';
    }

    struct ConfObject *obj = hashmap_find(stst->objects, string);
    if (obj) {
        printf("rhs is a value generator object!\n");
        // we only have one generator type: seqgen
        if (obj->type != CO_SEQGEN) {
            THROW("object type %s cannot be used as right-hand-side expression",
                    confobject_name_from_type(obj->type));
        }
        if (lhs->value_type != FT_TSNSEQ) {
            THROW("type of left-hand-side expression '%s' is invalid for TSN sequence generator",
                    fieldtype_name_from_type(lhs->value_type));
        }
        rhs->read = seq_generator;
        init_confvariable(rhs, CVT_GEN, FT_TSNSEQ, 0, 32);
        rhs->v.object.obj = obj->object;
        return true;
    }

    printf("rhs may be a constant...\n");
    // constant doesn't have a read function just a value
    init_confvariable(rhs, CVT_CONST, FT_UNKNOWN, lhs->value.bitoffset, lhs->value.bitcount);
    if (read_constant(&rhs->value, lhs->value_type, string)) {
        printf("rhs is a constant!\n");
        rhs->value_type = lhs->value_type;
        return true;
    } else {
        THROW("failed to parse '%s' as a constant", string);
    }
#undef THROW
}

// here we process the parameters for the action individually
static bool process_token(char *token, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "stream %s action %s: " msg "\n",           \
                stst->stream, stst->actions->text, ##__VA_ARGS__);  \
        return false;                                               \
    } while (0)

    struct StageState *stst = userdata;

    if (stst->actions->type) {
        switch (stst->actions->type) {
            case CA_ADD:
                if (stst->actions->d.add.pos == NULL) {
                    struct HeaderDescriptor *pos = header_list_find_by_name(stst->headers, token);
                    if (pos == NULL) {
                        THROW("no header named '%s' in the packet", token);
                    }
                    if (header_list_find_by_name(pos->next, token)) {
                        THROW("header name '%s' is ambiguous", token);
                    }
                    stst->actions->d.add.pos = pos;
                } else if (stst->actions->d.add.was_add == false) {
                    if (strcmp(token, "add") == 0) {
                        stst->actions->d.add.was_add = true;
                    } else {
                        THROW("the 'add' keyword is mising");
                    }
                } else if (stst->actions->d.add.newname == NULL) {
                    stst->actions->d.add.newname = strdup(token);
                    char *type = header_type_from_name(token);
                    stst->actions->d.add.id = protocol_id_from_type(type);
                    if (stst->actions->d.add.id < 0) {
                        THROW("type is invalid for new header'%s'", token);
                    }
                    stst->actions->d.add.newtype = type;
                    stst->actions->d.add.len = protocol_list[stst->actions->d.add.id].bytelength;
                } else {
                    char *lhs, *rhs;
                    struct ConfAssignment *a = calloc_struct(ConfAssignment);
                    a->next = stst->actions->d.add.assignments;
                    stst->actions->d.add.assignments = a;
                    a->text = strdup(token);
                    if (parse_assignment(token, &lhs, &rhs)) {
                        struct ProtocolField *f = protocol_get_field_by_name(stst->actions->d.add.id, lhs);
                        if (f == NULL) {
                            THROW("header %s has no field '%s'",
                                    stst->actions->d.add.newname, lhs);
                        }
                        init_confvariable(&a->lhs, CVT_FIELD, f->type, f->bitoffset, f->bitcount);
                        // we don't yet have a header index here, we fix it in process_action()
                        struct HeaderField *hf = new_headerfield(0, f->bitoffset, f->bitcount);
                        a->lhs.v.field.field = hf;

                        //TODO from here the rest is the same as in edit, try to unify with a function
                        if (!process_assignment_rhs(stst, &a->lhs, &a->rhs, rhs)) {
                            THROW("right-hand-side '%s' invalid", rhs);
                        }

                        // CVT_CONST already checked these, the other rhs types didn't
                        if ((a->lhs.value.bitoffset % 8) != (a->rhs.value.bitoffset % 8)) {
                            THROW("assignment has incompatible offsets");
                        }
                        if (a->lhs.value.bitcount != a->rhs.value.bitcount) {
                            THROW("assignment has incompatible bit counts");
                        }

                        // select consumer function for lhs
                        a->lhs.write = header_get_field_writer(a->lhs.v.field.field, &a->rhs.value);
                        if (a->lhs.write == NULL) {
                            THROW("header field cannot be written from this rhs");
                        }
                    } else {
                        THROW("invalid field assignment '%s'", token);
                    }
                }
                break;
            case CA_DEL:
                if (stst->actions->d.del.hdr) {
                    THROW("delete action takes only 1 parameter");
                } else {
                    struct HeaderDescriptor *del = header_list_find_by_name(stst->headers, token);
                    if (del) {
                        if (header_list_find_by_name(del->next, token)) {
                            THROW("header name '%s' is ambiguous", token);
                        }
                        stst->actions->d.del.hdr = del;
                    } else {
                        THROW("invalid header '%s' to delete", token);
                    }
                }
                break;
            case CA_DELAY:
                //TODO first argument is timestamp field
                //  TODO feri: we should always use the timestamp metadata
                //TODO second argument is a delay value (time)
                break;
            case CA_DROP:
                THROW("drop action doesn't take parameters");
                break;
            case CA_EDIT: {
                printf("edit token '%s'\n", token);
                char *lhs, *rhs;
                struct ConfAssignment *a = calloc_struct(ConfAssignment);
                a->next = stst->actions->d.edit.assignments;
                stst->actions->d.edit.assignments = a;
                a->text = strdup(token);
                if (parse_assignment(token, &lhs, &rhs)) {
                    if (!process_assignment_lhs(stst->headers, &a->lhs, lhs)) {
                        THROW("left-hand-side '%s' invalid", lhs);
                    }

                    if (!process_assignment_rhs(stst, &a->lhs, &a->rhs, rhs)) {
                        THROW("right-hand-side '%s' invalid", rhs);
                    }

                    // CVT_CONST already checked these, the other rhs types didn't
                    if ((a->lhs.value.bitoffset % 8) != (a->rhs.value.bitoffset % 8)) {
                        THROW("assignment has incompatible offsets");
                    }
                    if (a->lhs.value.bitcount != a->rhs.value.bitcount) {
                        THROW("assignment has incompatible bit counts");
                    }

                    // select consumer function for lhs
                    if (a->lhs.type == CVT_PACKET) {
                        a->lhs.write = packet_get_property_writer(a->lhs.v.meta.name, &a->rhs.value);
                        if (a->lhs.write == NULL) {
                            THROW("packet metadata '%s' cannot be written from this rhs",
                                    a->lhs.v.meta.name);
                        }
                    } else {
                        a->lhs.write = header_get_field_writer(a->lhs.v.field.field, &a->rhs.value);
                        if (a->lhs.write == NULL) {
                            THROW("header field cannot be written from this rhs");
                        }
                    }
                } else {
                    THROW("invalid assignment '%s'", token);
                }
                break; }
            case CA_ELIM:
                if (stst->actions->d.elim.rec == NULL) {
                    struct ConfObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        if (obj->type == CO_SEQREC) {
                            stst->actions->d.elim.rec = obj->object;
                        } else {
                            THROW("first argument of eliminate must be a recovery object");
                        }
                    } else {
                        THROW("first argument of eliminate must be a recovery object");
                    }
                } else {
                    THROW("the only argument of eliminate is the recovery object");
                    //TODO feri: we should always use the seq metadata
                    //TODO miki: we could have any rhs here but fine whatever
                }
                break;
            case CA_JUMP:
                if (stst->actions->d.jump.pipename == NULL) {
                    stst->actions->d.jump.pipename = strdup(token);
                } else {
                    THROW("the only argument is the name of an action pipeline");
                }
                break;
            case CA_POF:
                if (stst->actions->d.pof.pof == NULL) {
                    struct ConfObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        if (obj->type == CO_POF) {
                            stst->actions->d.pof.pof = obj;
                        } else {
                            THROW("pof first argument must be a pof object");
                        }
                    } else {
                        THROW("pof first argument must be a pof object");
                    }
                } else {
                    //TODO second argument is sequence field
                    //TODO exactly the same as in CA_ELIM
                    //TODO feri: we should always use the seq metadata
                }
                break;
            case CA_REPL:
                replicatelist_push_string(&stst->actions->d.repl.pipelines, strdup(token));
                break;
            case CA_SEND:
                if (stst->actions->d.send.iface) {
                    THROW("we can only send on one interface at once");
                } else {
                    struct Interface *iface = find_interface(stst, token);
                    if (iface == NULL) {
                        THROW("unknown interface '%s'", token);
                    }
                    //TODO dynconf: defer finalizing the iface pointer to assemble_actions() ?
                    //      that's the least of our problems when dynconf changes interfaces :(
                    stst->actions->d.send.iface = iface;
                }
                break;
        }
    } else {
        if        (strcmp(token, "after") == 0) {
            stst->actions->type = CA_ADD;
            stst->actions->d.add.beforeafter = ADD_AFTER;
        } else if (strcmp(token, "before") == 0) {
            stst->actions->type = CA_ADD;
            stst->actions->d.add.beforeafter = ADD_BEFORE;
        } else if (strcmp(token, "del") == 0) {
            stst->actions->type = CA_DEL;
        } else if (strcmp(token, "delay") == 0) {
            stst->actions->type = CA_DELAY;
        } else if (strcmp(token, "drop") == 0) {
            stst->actions->type = CA_DROP;
        } else if (strcmp(token, "edit") == 0) {
            stst->actions->type = CA_EDIT;
        } else if (strcmp(token, "eliminate") == 0) {
            stst->actions->type = CA_ELIM;
        } else if (strcmp(token, "jump") == 0) {
            stst->actions->type = CA_JUMP;
        } else if (strcmp(token, "pof") == 0) {
            stst->actions->type = CA_POF;
        } else if (strcmp(token, "replicate") == 0) {
            stst->actions->type = CA_REPL;
        } else if (strcmp(token, "send") == 0) {
            stst->actions->type = CA_SEND;
        } else {
            struct ConfObject *obj = hashmap_find(stst->objects, token);
            if (obj) {
                switch (obj->type) {
                    case CO_SEQGEN:
                        THROW("sequence generator cannot be used as action");
                        break;
                    case CO_SEQREC:
                        stst->actions->type = CA_ELIM;
                        stst->actions->d.elim.rec = obj->object;
                        break;
                    case CO_POF:
                        stst->actions->type = CA_POF;
                        stst->actions->d.pof.pof = obj;
                        break;
                }
            } else {
                char *pstring = inisection_get(stst->streams_sec, token);
                if (pstring) {
                    stst->actions->type = CA_JUMP;
                    stst->actions->d.jump.pipename = strdup(token);
                } else {
                    THROW("action name invalid");
                }
            }
        }
    }

    return true;
#undef THROW
}

static bool process_stage(char *stage, void *userdata);

// here we do processing that needs all the parameters of the action
static bool process_action(struct StageState *stst)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "stream %s action %s: " msg "\n",           \
                stst->stream, stst->actions->text, ##__VA_ARGS__);  \
        return false;                                               \
    } while (0)

    char editbuf[64];
    switch (stst->actions->type) {
        case CA_ADD:
            printf("CA_ADD: %s %s %s %s\n",
                    stst->actions->d.add.beforeafter==ADD_BEFORE?"before":"after",
                    stst->actions->d.add.pos->name,
                    stst->actions->d.add.newname, stst->actions->d.add.newtype);
            if (stst->actions->d.add.newname == NULL) {
                THROW("no new header name");
            }
            if (stst->actions->d.add.pos == NULL) {
                THROW("no existing header name");
            }
            if (stst->actions->d.add.beforeafter == ADD_UNKNOWN) {
                THROW("no header position specified");
            }

            struct HeaderDescriptor *newheader = calloc_struct(HeaderDescriptor);
            newheader->type = strdup(stst->actions->d.add.newtype);
            newheader->name = strdup(stst->actions->d.add.newname);
            newheader->id = stst->actions->d.add.id;

            // add newheader to stst->headers at the designated position
            struct HeaderDescriptor *prevheader, *nextheader;
            if (stst->actions->d.add.beforeafter == ADD_BEFORE) {
                nextheader = stst->actions->d.add.pos;
                if (nextheader == stst->headers) {
                    prevheader = NULL;
                } else {
                    prevheader = stst->headers;
                    while (prevheader->next != stst->actions->d.add.pos)
                        prevheader = prevheader->next;
                }
            } else {
                prevheader = stst->actions->d.add.pos;
                nextheader = stst->actions->d.add.pos->next;
            }

            unsigned pos_idx = header_index(stst->headers, stst->actions->d.add.pos);
            if (stst->actions->d.add.beforeafter == ADD_AFTER) pos_idx++;
            stst->actions->d.add.pos_idx = pos_idx;
            for (struct ConfAssignment *a=stst->actions->d.add.assignments; a; a=a->next) {
                a->lhs.v.field.field->header_idx = pos_idx;
            }

            newheader->next = nextheader;
            if (prevheader) {
                prevheader->next = newheader;
            } else {
                stst->headers = newheader;
            }

            // split off the header assignments into a new edit action
            struct ConfAction *edit = calloc_struct(ConfAction);
            edit->type = CA_EDIT;
            edit->text = strdup(stst->actions->text);
            edit->d.edit.assignments = stst->actions->d.add.assignments;
            stst->actions->d.add.assignments = NULL;
            edit->next = stst->actions;
            stst->actions = edit;

            //TODO if the new header has a FT_TSNSEQ field, automatically create
            //      edit newheader.seq=meta.seq
            //      TODO unless stst->actions->d.add.assignments already has an assignment for it
            //TODO if the new header has a FT_TSNTSTAMP field, automatically create
            //      edit newheader.tstamp=meta.tstamp
            //      TODO unless stst->actions->d.add.assignments already has an assignment for it

            // set the nexthdr field of newheader
            if (protocol_list[newheader->id].get_nexthdr != NULL) {
                struct Protocol *newpr = &protocol_list[newheader->id];
                unsigned nexthdr_idx = newpr->nexthdr_idx;
                struct ProtocolField *f = &newpr->header_fields[nexthdr_idx];
                struct ConfAssignment *a = NULL;
                if (nextheader) {
                    uint16_t nexthdrnum;
                    if (!newpr->get_nexthdr(&nexthdrnum, nextheader->id)) {
                        THROW("header type %s cannot have type %s as next header",
                                newheader->type, nextheader->type);
                    }

                    // create an assignment: newheader.nexthdr = nexthdrnum constant
                    nexthdrnum = ntohs(nexthdrnum); // we need it in host order
                    a = calloc_struct(ConfAssignment);
                    a->next = edit->d.edit.assignments;
                    edit->d.edit.assignments = a;
                    snprintf(editbuf, 64, "add sets %s.%s=0x%.4x",
                            newheader->name, f->name, nexthdrnum);
                    a->text = strdup(editbuf);
                    init_confvariable(&a->lhs, CVT_FIELD, f->type, f->bitoffset, f->bitcount);
                    init_confvariable(&a->rhs, CVT_CONST, f->type, f->bitoffset, f->bitcount);
                    prepare_constant_number(&a->rhs.value, nexthdrnum);
                } else {
                    // let's see if we can copy prevheader's field
                    if (!prevheader) {
                        THROW("need to set nexthdr but no information from previous header");
                    }
                    struct Protocol *prevpr = &protocol_list[prevheader->id];
                    unsigned prevhdr_idx = prevpr->nexthdr_idx;
                    struct ProtocolField *pf = &prevpr->header_fields[prevhdr_idx];
                    if (newpr->get_nexthdr != prevpr->get_nexthdr) {
                        THROW("can't copy nexthdr type from previous header");
                    }

                    // create a copy assignment: newheader.nexthdr = prevheader.nexthdr
                    a = calloc_struct(ConfAssignment);
                    a->next = edit->d.edit.assignments;
                    edit->d.edit.assignments = a;
                    snprintf(editbuf, 64, "add sets %s.%s=%s.%s",
                            newheader->name, f->name, prevheader->name, pf->name);
                    a->text = strdup(editbuf);
                    init_confvariable(&a->lhs, CVT_FIELD, f->type, f->bitoffset, f->bitcount);
                    init_confvariable(&a->rhs, CVT_FIELD, pf->type, pf->bitoffset, pf->bitcount);
                    struct HeaderField *phf = new_headerfield(pos_idx-1, pf->bitoffset, pf->bitcount);
                    a->rhs.v.field.field = phf;
                    a->rhs.read = header_get_field_reader(&a->lhs.value, phf);
                    if (a->rhs.read == NULL) {
                        THROW("can't copy nexthdr type from previous header");
                    }
                }
                struct HeaderField *nhf = new_headerfield(pos_idx, f->bitoffset, f->bitcount);
                a->lhs.v.field.field = nhf;
                a->lhs.write = header_get_field_writer(nhf, &a->rhs.value);
                if (a->lhs.write == NULL) {
                    THROW("can't copy nexthdr type from previous header");
                }
            }

            // set nexthdr of prevheader with newheader's type
            if (prevheader && protocol_list[prevheader->id].get_nexthdr != NULL) {
                struct Protocol *prevpr = &protocol_list[prevheader->id];
                unsigned prevhdr_idx = prevpr->nexthdr_idx;
                struct ProtocolField *pf = &prevpr->header_fields[prevhdr_idx];
                uint16_t nexthdrnum;
                if (!prevpr->get_nexthdr(&nexthdrnum, newheader->id)) {
                    THROW("header type %s cannot have type %s as next header",
                            prevheader->type, newheader->type);
                }

                // create an assignment: nexthdr field = nexthdrnum constant
                nexthdrnum = ntohs(nexthdrnum); // we need it in host order
                struct ConfAssignment *a = calloc_struct(ConfAssignment);
                a->next = edit->d.edit.assignments;
                edit->d.edit.assignments = a;
                snprintf(editbuf, 64, "add sets %s.%s=0x%.4x",
                        prevheader->name, pf->name, nexthdrnum);
                a->text = strdup(editbuf);
                init_confvariable(&a->lhs, CVT_FIELD, pf->type, pf->bitoffset, pf->bitcount);
                init_confvariable(&a->rhs, CVT_CONST, pf->type, pf->bitoffset, pf->bitcount);
                prepare_constant_number(&a->rhs.value, nexthdrnum);
                struct HeaderField *phf = new_headerfield(pos_idx-1, pf->bitoffset, pf->bitcount);
                a->lhs.v.field.field = phf;
                a->lhs.write = header_get_field_writer(phf, &a->rhs.value);
                if (a->lhs.write == NULL) {
                    THROW("can't set nexthdr type in previous header");
                }
            }

            process_action(stst); // now edit is the newest action
            break;
        case CA_DEL:
            if (stst->actions->d.del.hdr == NULL) {
                THROW("no header to delete");
            }
            struct HeaderDescriptor *del = stst->actions->d.del.hdr;
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
            stst->actions->d.del.idx = idx;

            // handle the nexthdr field of prev
            struct ConfAssignment *a = NULL;
            if (prev) {
                struct Protocol *prevpr = &protocol_list[prev->id];
                unsigned prevhdr_idx = prevpr->nexthdr_idx;
                struct ProtocolField *pf = &prevpr->header_fields[prevhdr_idx];
                if (prevpr->get_nexthdr) {
                    if (del->next) {
                        uint16_t nexthdrnum;
                        if (!prevpr->get_nexthdr(&nexthdrnum, del->next->id)) {
                            THROW("header type %s cannot have type %s as next header",
                                    prev->type, del->next->type);
                        }

                        // create an assignment: nexthdr field = nexthdrnum constant
                        a = calloc_struct(ConfAssignment);
                        snprintf(editbuf, 64, "del sets %s.%s=0x%.4x",
                                prev->name, pf->name, nexthdrnum);
                        a->text = strdup(editbuf);
                        init_confvariable(&a->lhs, CVT_FIELD, pf->type, pf->bitoffset, pf->bitcount);
                        init_confvariable(&a->rhs, CVT_CONST, pf->type, pf->bitoffset, pf->bitcount);
                        prepare_constant_number(&a->rhs.value, nexthdrnum);
                    } else {
                        // let's see if we can copy del's field
                        struct Protocol *delpr = &protocol_list[del->id];
                        unsigned delhdr_idx = delpr->nexthdr_idx;
                        struct ProtocolField *df = &delpr->header_fields[delhdr_idx];
                        if (prevpr->get_nexthdr != delpr->get_nexthdr) {
                            THROW("can't copy nexthdr type from deleted to previous header");
                        }

                        // create a copy assignment:  nexthdr field = delheader's field
                        a = calloc_struct(ConfAssignment);
                        snprintf(editbuf, 64, "del sets %s.%s=%s.%s",
                                prev->name, pf->name, del->name, df->name);
                        a->text = strdup(editbuf);
                        init_confvariable(&a->lhs, CVT_FIELD, pf->type, pf->bitoffset, pf->bitcount);
                        init_confvariable(&a->rhs, CVT_FIELD, df->type, df->bitoffset, df->bitcount);
                        struct HeaderField *dhf = new_headerfield(idx, df->bitoffset, df->bitcount);
                        a->rhs.v.field.field = dhf;
                        a->rhs.read = header_get_field_reader(&a->lhs.value, dhf);
                        if (a->rhs.read == NULL) {
                            THROW("can't copy nexthdr type from deleted to previous header");
                        }
                    }
                    struct HeaderField *phf = new_headerfield(idx-1, pf->bitoffset, pf->bitcount);
                    a->lhs.v.field.field = phf;
                    a->lhs.write = header_get_field_writer(phf, &a->rhs.value);
                    if (a->lhs.write == NULL) {
                        THROW("can't copy nexthdr type from deleted to previous header");
                    }
                }

                prev->next = del->next;
            } else {
                stst->headers = del->next;
            }
            del->next = NULL;
            delete_header_list(del);

            if (a) {
                struct ConfAction *dedit = calloc_struct(ConfAction);
                dedit->type = CA_EDIT;
                dedit->text = strdup(stst->actions->text);
                dedit->d.edit.assignments = a;
                dedit->next = stst->actions;
                stst->actions = dedit;
                process_action(stst); // now dedit is the newest action

                // swap dedit and del so we are editing before deleting
                stst->actions = dedit->next;
                dedit->next = stst->actions->next;
                stst->actions->next = dedit;
            }
            break;
        case CA_DELAY:
            //TODO check that first param was a valid timestamp field
            //      need to find the header by name
            //  TODO feri: we should always use the timestamp metadata
            //TODO check that second param was a valid time constant
            break;
        case CA_DROP:
            stst->had_final = true;
            break;
        case CA_EDIT:
            printf("CA_EDIT: %s\n", stst->actions->text);
            if (stst->actions->d.edit.assignments != NULL) {
                REVERSE_LIST(stst->actions->d.edit.assignments);
            } else {
                THROW("no assignments in edit");
            }
            break;
        case CA_ELIM:
            if (stst->actions->d.elim.rec == NULL) {
                THROW("eliminate needs a sequence recovery object");
            }
            //TODO we could have any rhs here
            struct Value val = {NULL, 0, 32};
            stst->actions->d.elim.seq_producer = packet_get_property_reader("seq", &val);
            stst->actions->d.elim.seq_producer_state = NULL;
            break;
        case CA_JUMP:
            printf("CA_JUMP: %s\n", stst->actions->text);
            if (stst->actions->d.jump.pipename == NULL) {
                THROW("no action pipeline to jump to");
            }
            char *pipestring = inisection_get(stst->streams_sec, stst->actions->d.jump.pipename);
            if (pipestring) {
                struct StageState jstst = *stst;
                jstst.stream = stst->actions->d.jump.pipename;
                jstst.actions = NULL;
                //TODO limit recursion depth with a counter in stst
                if (!foreach_stages(pipestring, process_stage, &jstst)) {
                    THROW("failed to process pipeline '%s'", stst->actions->d.jump.pipename);
                }
                if (jstst.actions == NULL) {
                    THROW("no actions in pipeline '%s'", stst->actions->d.jump.pipename);
                }

                // replace jump with the newly read action list
                struct ConfAction *newend = jstst.actions;
                while (newend->next) newend = newend->next;
                newend->next = stst->actions->next;
                struct ConfAction *jump = stst->actions;
                jump->next = NULL;
                stst->actions = jstst.actions;
                delete_confaction_list(jump);
            } else {
                THROW("action pipeline '%s' not found", stst->actions->d.jump.pipename);
            }
            stst->had_final = true;
            break;
        case CA_POF:
            //TODO
            break;
        case CA_REPL:
            printf("CA_REPL: %s\n", stst->actions->text);
            if (stst->actions->d.repl.pipelines == NULL) {
                THROW("no pipelines specified");
            }
            struct ReplicateList *p = stst->actions->d.repl.pipelines;
            while (p) {
                printf(" branch '%s'\n", p->string);
                char *pstring = inisection_get(stst->streams_sec, p->string);
                if (pstring == NULL) {
                    THROW("pipeline '%s' not found", p->string);
                }
                struct StageState pstst = *stst;
                pstst.stream = p->string;
                pstst.headers = copy_header_list(stst->headers);
                pstst.actions = NULL;
                if (!foreach_stages(pstring, process_stage, &pstst)) {
                    delete_header_list(pstst.headers);
                    THROW("failed to process pipeline '%s'", p->string);
                }
                if (pstst.actions == NULL) {
                    delete_header_list(pstst.headers);
                    THROW("no actions in pipeline '%s'", p->string);
                }
                delete_header_list(pstst.headers);
                p->actions = pstst.actions;
                REVERSE_LIST(p->actions);
                p = p->next;
            }
            REVERSE_LIST(stst->actions->d.repl.pipelines);
            stst->had_final = true;
            break;
        case CA_SEND:
            if (stst->actions->d.send.iface == NULL) {
                THROW("no send interface specified");
            }
            break;
    }
    return true;
#undef THROW
}

static bool process_stage(char *stage, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->had_final) {
        fprintf(stderr, "can't have more actions after %s\n",
                confaction_name_from_type(stst->actions->type));
        return false;
    }

    struct ConfAction *newaction = calloc_struct(ConfAction);
    newaction->next = stst->actions;
    stst->actions = newaction;
    newaction->text = strdup(stage);

    if (!foreach_tokens(stage, process_token, stst)) {
        fprintf(stderr, "failed to process action parameters '%s'\n", newaction->text);
        return false;
    }

    if (stst->actions->type == 0) {
        fprintf(stderr, "no action in '%s'\n", newaction->text);
        return false;
    }

    // now we have all arguments for the action, let's process it properly
    if (!process_action(stst)) {
        fprintf(stderr, "failed to process action '%s'\n", newaction->text);
        return false;
    }

    return true;
}

struct ConfAction *parse_actions_line(const char *stream, char *line,
        const struct HeaderDescriptor *headers,
        struct Interface *ifaces, unsigned ifcount,
        struct HashMap *objects, struct IniSection *streams_sec)
{
    struct StageState stst = {
        .stream = stream,
        .actions = NULL,
        .headers = copy_header_list(headers),
        .ifaces = ifaces,
        .ifcount = ifcount,
        .objects = objects,
        .streams_sec = streams_sec,
        .had_final = false,
    };
    if (!foreach_stages(line, process_stage, &stst)) {
        fprintf(stderr, "failed to process actions line for stream '%s'\n", stream);
        delete_header_list(stst.headers);
        delete_confaction_list(stst.actions);
        return NULL;
    }

    if (stst.actions == NULL) {
        fprintf(stderr, "no actions in actions line for stream '%s'\n", stream);
        delete_header_list(stst.headers);
        return NULL;
    }

    REVERSE_LIST(stst.actions);

    //TODO perform optimization passes on the action list
    //      e.g. merge subsequent Edit actions

    delete_header_list(stst.headers);

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
            free(del->lhs.v.field.field);
        if (del->rhs.type == CVT_FIELD)
            free(del->rhs.v.field.field);
        if (del->lhs.type == CVT_PACKET)
            free(del->lhs.v.meta.name);
        if (del->rhs.type == CVT_PACKET)
            free(del->rhs.v.meta.name);
        free(del);
    }
}

static void delete_replicatelist(struct ReplicateList *pipelines)
{
    while (pipelines) {
        struct ReplicateList *del = pipelines;
        pipelines = pipelines->next;
        free(del->string);
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
            case CA_ADD:
                free(del->d.add.newname);
                free(del->d.add.newtype);
                delete_confassignments(del->d.add.assignments);
                break;
            case CA_DEL:
                break;
            case CA_DELAY:
                break;
            case CA_DROP:
                break;
            case CA_EDIT:
                delete_confassignments(del->d.edit.assignments);
                break;
            case CA_ELIM:
                break;
            case CA_JUMP:
                free(del->d.jump.pipename);
                break;
            case CA_POF:
                break;
            case CA_REPL:
                delete_replicatelist(del->d.repl.pipelines);
                break;
            case CA_SEND:
                break;
        }
        free(del);
    }

    return NULL;
}

static struct HeaderFieldAssign *assemble_fieldassigns(struct ConfAssignment *list, unsigned *assigncount)
{
    unsigned count = 0;
    for (struct ConfAssignment *l=list; l; l=l->next) count++;
    if (count == 0) {
        *assigncount = 0;
        return NULL;
    }
    struct HeaderFieldAssign *ret = calloc_struct_array(HeaderFieldAssign, count);

    unsigned i=0;
    for (struct ConfAssignment *l=list; l; l=l->next) {
        struct HeaderFieldAssign *a = ret+i;
        a->text = strdup(l->text);
        a->assign = l->lhs.write;
        if (l->lhs.type == CVT_FIELD)
            a->target = *l->lhs.v.field.field;
        a->generator = l->rhs.read;
        switch (l->rhs.type) {
            case CVT_UNDEF:
                fprintf(stderr, "assign '%s' source is undefined\n", l->text);
                return NULL;
            case CVT_FIELD:
                a->generator_state = l->rhs.v.field.field;
                break;
            case CVT_PACKET:
                // this only needs the packet
                break;
            case CVT_CONST:
                a->constant = l->rhs.value;
                unsigned len = DIVCEIL(a->constant.bitoffset + a->constant.bitcount, 8);
                a->constant.value = memdup(l->rhs.value.value, len);
                break;
            case CVT_IFACE:
                a->generator_state = l->rhs.v.iface.iface;
                break;
            case CVT_GEN:
                a->generator_state = l->rhs.v.object.obj;
                break;
        }

        i += 1;
    }

    *assigncount = count;
    return ret;
}

struct Action *assemble_actions(const struct ConfAction *ca_list, unsigned *action_count)
{
    //TODO define THROW
    unsigned count = 0;
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) count++;
    if (count == 0) {
        *action_count = 0;
        return NULL;
    }

    struct Action *ret = calloc_struct_array(Action, count);

    unsigned a = 0;
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) {
        switch (ca->type) {
            case CA_ADD:
                create_action_add(ret+a, ca->d.add.pos_idx, ca->d.add.id, ca->d.add.len, ca->text);
                break;
            case CA_DEL:
                create_action_del(ret+a, ca->d.del.idx, ca->text);
                break;
            case CA_DELAY:
                //TODO create_action_delay(ret+a, ca->d.delay.xxxx);
                break;
            case CA_DROP:
                create_action_drop(ret+a, ca->text);
                break;
            case CA_EDIT: {
                unsigned acount;
                struct HeaderFieldAssign *assigns = assemble_fieldassigns(ca->d.edit.assignments, &acount);
                if (assigns == NULL) {
                    //TODO cleanup on error
                    fprintf(stderr, "could not assemble the field assignments for edit action\n");
                    return NULL;
                }
                create_action_edit(ret+a, assigns, acount, ca->text);
                break; }
            case CA_ELIM:
                create_action_elim(ret+a, ca->d.elim.rec,
                        ca->d.elim.seq_producer, ca->d.elim.seq_producer_state, ca->text);
                break;
            case CA_JUMP:
                fprintf(stderr, "assemble_actions() jump should have been inlined\n");
                //TODO cleanup on error
                return NULL;
            case CA_POF:
                //TODO create_action_pof()
                break;
            case CA_REPL: {
                struct PipelineList *pipes = NULL;
                for (struct ReplicateList *r=ca->d.repl.pipelines; r; r=r->next) {
                    unsigned r_action_count;
                    struct Action *r_actions = assemble_actions(r->actions, &r_action_count);
                    if (!r_actions) {
                        fprintf(stderr, "failed to assemble actions for stream %s\n", r->string);
                        //TODO cleanup on error
                        return NULL;
                    }
                    struct Pipeline *pipe = new_pipeline(r_actions, r_action_count);
                    if (!pipe) { //TODO this never happens
                    }
                    pipeline_ref(pipe);

                    struct PipelineList *p = calloc_struct(PipelineList);
                    p->pipe = pipe;
                    p->text = r->string;
                    p->next = pipes;
                    pipes = p;
                }
                REVERSE_LIST(pipes);
                create_action_repl(ret+a, pipes, ca->text);
                break; }
            case CA_SEND:
                create_action_send(ret+a, ca->d.send.iface, ca->text);
                break;
        }
        a++;
    }

    *action_count = count;
    return ret;
}

void confactions_print(const struct ConfAction *ca_list)
{
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) {
        fprintf(stderr, "ConfAction %s '%s'\n",
                confaction_name_from_type(ca->type), ca->text);
        switch (ca->type) {
            case CA_ADD:
                printf("  new name %s type %s id %d len %u index %u\n",
                        ca->d.add.newname, ca->d.add.newtype, ca->d.add.id, ca->d.add.len,
                        ca->d.add.pos_idx);
                break;
            case CA_DEL:
                printf("  index %u\n", ca->d.del.idx);
                break;
            case CA_DELAY:
                printf("  \n");
                break;
            case CA_DROP:
                printf("  \n");
                break;
            case CA_EDIT:
                for (struct ConfAssignment *a=ca->d.edit.assignments; a; a=a->next) {
                    printf("  %s\n", a->text);
                    printf("    lhs type %s valuetype %s bitoffset %u bitcount %u\n",
                            variabletype_name_from_type(a->lhs.type),
                            fieldtype_name_from_type(a->lhs.value_type),
                            a->lhs.value.bitoffset, a->lhs.value.bitcount);
                    printf("      lhs read %p write %p\n", a->lhs.read, a->lhs.write);
                    if (a->lhs.type == CVT_FIELD) {
                        printf("      index %u\n", a->lhs.v.field.field->header_idx);
                    }
                    printf("    rhs type %s valuetype %s bitoffset %u bitcount %u\n",
                            variabletype_name_from_type(a->rhs.type),
                            fieldtype_name_from_type(a->rhs.value_type),
                            a->rhs.value.bitoffset, a->rhs.value.bitcount);
                    printf("      rhs read %p write %p\n", a->rhs.read, a->rhs.write);
                    if (a->rhs.type == CVT_FIELD) {
                        printf("      index %u\n", a->rhs.v.field.field->header_idx);
                    } else if (a->rhs.type == CVT_CONST) {
                        printf("      constant:");
                        unsigned bytes = DIVCEIL(a->rhs.value.bitoffset + a->rhs.value.bitcount, 8);
                        unsigned char *cst = a->rhs.value.value;
                        for (unsigned i=0; i<bytes; i++) {
                            printf(" 0x%.2x", cst[i]);
                        }
                        printf("\n");
                    }
                }
                break;
            case CA_ELIM:
                printf("  \n");
                break;
            case CA_JUMP:
                printf("  %s\n", ca->d.jump.pipename);
                break;
            case CA_POF:
                printf("  \n");
                break;
            case CA_REPL:
                for (struct ReplicateList *p=ca->d.repl.pipelines; p; p=p->next) {
                    printf("  vvvvvvvv %s vvvvvvvv\n", p->string);
                    confactions_print(p->actions);
                    printf("  ^^^^^^^^ %s ^^^^^^^^\n", p->string);
                }
                break;
            case CA_SEND:
                printf("  iface %s\n", ca->d.send.iface ? ca->d.send.iface->name : "UNKNOWN");
                break;
        }
    }
}
