
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

enum ConfActionType {
    CA_UNDEF,
    CA_ADD,
    CA_DEL,
    CA_DELAY,
    CA_DROP,
    CA_EDIT,
    CA_ELIM,
    CA_JUMP,
    CA_POF,
    CA_READSEQ,
    CA_READTSTAMP,
    CA_REPL,
    CA_SEND,
    CA_SEQGEN,
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
        } elim;
        struct {
            char *pipename;
        } jump;
        struct {
            struct HeaderDescriptor *hdr;
            struct HeaderField *field;
        } meta; // read/write seq/tstamp
        struct {
            struct Pof *pof;
        } pof;
        struct {
            struct ReplicateList *pipelines;
        } repl;
        struct {
            struct Interface *iface;
        } send;
        struct {
            struct SequenceGenerator *gen;
        } seq;
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
    bool seq_set; // true if we had an action that sets packet->sequence
    //bool tstamp_set; //TODO instead: packet->timestamp should be initialized from packet->recv_time
};


static void replicatelist_push_string(struct ReplicateList **list, char *string)
{
    struct ReplicateList *l = calloc_struct(ReplicateList);
    l->string = string;
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

// @returns the position of @pos in the linked list @headers
// @pos must be in the linked list!
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
        case CA_JUMP:
            return "Jump";
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
static bool process_assignment_lhs(struct HeaderDescriptor *headers, struct ConfVariable *lhs, char *string)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, msg "\n", ##__VA_ARGS__);                   \
        return false;                                               \
    } while (0)

    char *hdr, *field;
    if (parse_fieldname(string, &hdr, &field)) {
        struct HeaderDescriptor *h = header_list_find_by_name(headers, hdr);
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
        lhs->v.header.field = new_headerfield(header_index(headers, h), f);
    } else {
        THROW("left-hand-side '%s' of the assignment is invalid", string);
    }
    return true;
#undef THROW
}

static void dummy_constant_reader(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    (void)state;
    (void)consumer;
    (void)consumer_state;
    (void)p;
}

// @returns a value reader function or NULL on error
// @returns dummy reader for CVT_CONST
static value_producer *process_assignment_rhs(struct StageState *stst, const struct ConfVariable *lhs,
        struct ConfVariable *rhs, char *string)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, msg "\n", ##__VA_ARGS__);                   \
        return NULL;                                                \
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
            const struct ProtocolField *f = protocol_get_field_by_name(h->id, val);
            if (f) {
                printf("rhs is a header field!\n");
                if (lhs->value_type != f->type) {
                    THROW("types of left-hand-side %s and right-hand-side %s don't match",
                            fieldtype_name_from_type(lhs->value_type), fieldtype_name_from_type(f->type));
                }
                init_confvariable(rhs, CVT_FIELD, f);
                struct HeaderField *hf = new_headerfield(header_index(stst->headers, h), f);
                rhs->v.header.field = hf;
                value_producer *read = header_get_field_reader(&lhs->value, hf);
                if (read == NULL) {
                    //TODO can we print the name of lhs?
                    THROW("cannot read field %s.%s into the left-hand-side expression", key, val);
                }
                return read;
            } else {
                THROW("header %s has no field %s", key, val);
            }
        }

        struct Interface *iface = find_interface(stst, key);
        if (iface) {
            printf("rhs is an interface!\n");
            if (iface->get_property_reader) {
                value_producer *read = iface->get_property_reader(iface, val, lhs->value_type, &lhs->value);
                if (read == NULL) {
                    THROW("interface %s has no property named '%s'", iface->name, val);
                }
                init_confvariable_full(rhs, CVT_IFACE,
                        lhs->value_type, lhs->value.bitoffset%8, lhs->value.bitcount);
                rhs->v.iface.iface = iface;
                rhs->v.iface.property = strdup(val);
                return read;
            } else {
                THROW("interface %s has no queryable property", iface->name);
            }
        }

        // if nothing matched, restore the dot
        val[-1] = '.';
    }

    printf("rhs may be a constant...\n");
    // constant doesn't have a read function just a value
    init_confvariable_full(rhs, CVT_CONST, FT_UNKNOWN, lhs->value.bitoffset, lhs->value.bitcount);
    if (read_constant(&rhs->value, lhs->value_type, string)) {
        printf("rhs is a constant!\n");
        rhs->value_type = lhs->value_type;
        return dummy_constant_reader;
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

    //TODO reindent this
        switch (stst->actions->type) {
            case CA_UNDEF:
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
                } else if (strcmp(token, "readseq") == 0) {
                    stst->actions->type = CA_READSEQ;
                } else if (strcmp(token, "readtstamp") == 0) {
                    stst->actions->type = CA_READTSTAMP;
                } else if (strcmp(token, "replicate") == 0) {
                    stst->actions->type = CA_REPL;
                } else if (strcmp(token, "send") == 0) {
                    stst->actions->type = CA_SEND;
                } else if (strcmp(token, "seqgen") == 0) {
                    stst->actions->type = CA_SEND;
                } else if (strcmp(token, "writeseq") == 0) {
                    stst->actions->type = CA_WRITESEQ;
                } else if (strcmp(token, "writetstamp") == 0) {
                    stst->actions->type = CA_WRITETSTAMP;
                } else {
                    struct ConfObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        switch (obj->type) {
                            case CO_SEQGEN:
                                stst->actions->type = CA_SEQGEN;
                                stst->actions->d.seq.gen = obj->object;
                                break;
                            case CO_SEQREC:
                                stst->actions->type = CA_ELIM;
                                stst->actions->d.elim.rec = obj->object;
                                break;
                            case CO_POF:
                                stst->actions->type = CA_POF;
                                stst->actions->d.pof.pof = obj->object;
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
                break;
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
                    free(type);
                    if (stst->actions->d.add.id < 0) {
                        THROW("type is invalid for new header'%s'", token);
                    }
                    stst->actions->d.add.len = protocol_list[stst->actions->d.add.id].bytelength;
                } else {
                    char *lhs, *rhs;
                    struct ConfAssignment *a = calloc_struct(ConfAssignment);
                    a->next = stst->actions->d.add.assignments;
                    stst->actions->d.add.assignments = a;
                    a->text = strdup(token);
                    if (parse_assignment(token, &lhs, &rhs)) {
                        const struct ProtocolField *f = protocol_get_field_by_name(stst->actions->d.add.id, lhs);
                        if (f == NULL) {
                            THROW("header %s has no field '%s'",
                                    stst->actions->d.add.newname, lhs);
                        }
                        init_confvariable(&a->lhs, CVT_FIELD, f);
                        // we don't yet have a header index here, we fix it in process_action()
                        a->lhs.v.header.field = new_headerfield(0, f);

                        a->read = process_assignment_rhs(stst, &a->lhs, &a->rhs, rhs);
                        if (a->read == NULL) {
                            THROW("right-hand-side '%s' invalid", rhs);
                        }

                        // select consumer function for lhs
                        a->write = header_get_field_writer(a->lhs.v.header.field, &a->rhs.value);
                        if (a->write == NULL) {
                            THROW("header field cannot be written from this rhs");
                        }
                    } else {
                        THROW("invalid field assignment '%s'", token);
                    }
                }
                break;
            case CA_DEL:
                if (stst->actions->d.del.hdr == NULL) {
                    struct HeaderDescriptor *del = header_list_find_by_name(stst->headers, token);
                    if (del) {
                        if (header_list_find_by_name(del->next, token)) {
                            THROW("header name '%s' is ambiguous", token);
                        }
                        stst->actions->d.del.hdr = del;
                    } else {
                        THROW("invalid header '%s' to delete", token);
                    }
                } else {
                    THROW("delete action takes only 1 parameter");
                }
                break;
            case CA_DELAY:
                if (stst->actions->d.delay.timestamp_field.header_idx == 0) {
                  printf("Token %s\n", token);
                  struct HeaderDescriptor *delay_hdr = header_list_find_by_name(stst->headers, token);
                  if (delay_hdr) {
                      if (header_list_find_by_name(delay_hdr->next, token)) {
                          THROW("delay header name '%s' is ambiguous", token);
                      }
                      int hdrtype = delay_hdr->id;
                      int field_idx = -1;
                      for (unsigned i=0; i<protocol_list[hdrtype].header_field_count; i++) {
                          if (protocol_list[hdrtype].header_fields[i].type == FT_TSNTSTAMP) {
                              field_idx = i;
                              break;
                          }
                      }
                      if(field_idx == -1) THROW("no timestamp field in header '%s'", token);
                      stst->actions->d.delay.timestamp_field.header_idx = header_index(stst->headers, delay_hdr);
                      stst->actions->d.delay.timestamp_field.bitoffset = protocol_list[hdrtype].header_fields[field_idx].bitoffset;
                      stst->actions->d.delay.timestamp_field.bitcount = protocol_list[hdrtype].header_fields[field_idx].bitcount;
                      printf("bitcount %d offset %d ", stst->actions->d.delay.timestamp_field.bitcount, stst->actions->d.delay.timestamp_field.bitoffset);
                  } else {
                      THROW("invalid header '%s' to delay", token);
                  }
                } else {
                  init_confvariable_full(&stst->actions->d.delay.delay_value, CVT_CONST, FT_NUMBER, stst->actions->d.delay.timestamp_field.bitoffset, stst->actions->d.delay.timestamp_field.bitcount );
                  read_constant(&stst->actions->d.delay.delay_value.value, FT_NUMBER, token);
                  //THROW("delay action requires a header parameter");
                }
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

                    a->read = process_assignment_rhs(stst, &a->lhs, &a->rhs, rhs);
                    if (a->read == NULL) {
                        THROW("right-hand-side '%s' invalid", rhs);
                    }

                    // select consumer function for lhs
                    a->write = header_get_field_writer(a->lhs.v.header.field, &a->rhs.value);
                    if (a->write == NULL) {
                        THROW("header field cannot be written from this rhs");
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
                            stst->actions->d.pof.pof = obj->object;
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
                if (stst->actions->d.meta.hdr == NULL) {
                    struct HeaderDescriptor *hdr = header_list_find_by_name(stst->headers, token);
                    if (hdr) {
                        if (header_list_find_by_name(hdr->next, token)) {
                            THROW("header name '%s' is ambiguous", token);
                        }
                        stst->actions->d.meta.hdr = hdr;
                    } else {
                        THROW("invalid header '%s'", token);
                    }
                } else {
                    THROW("this action only takes one argument");
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
            case CA_SEQGEN:
                if (stst->actions->d.seq.gen == NULL) {
                    struct ConfObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        if (obj->type == CO_SEQGEN) {
                            stst->actions->d.seq.gen = obj->object;
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
        }

    return true;
#undef THROW
}

// set nexthdr value in @dstheader based on the type of @typeheader
static struct ConfAssignment *assign_nexthdrid_from_header_type(const char *action_name,
        const struct HeaderDescriptor *dstheader, unsigned dstpos,
        const struct HeaderDescriptor *typeheader)
{
    char editbuf[64];
    uint16_t nexthdrnum;
    const struct Protocol *dstpr = &protocol_list[dstheader->id];
    unsigned dst_idx = dstpr->nexthdr_idx;
    const struct ProtocolField *dstf = &dstpr->header_fields[dst_idx];
    if (!dstpr->get_nexthdr(&nexthdrnum, typeheader->id)) {
        return NULL;
    }

    nexthdrnum = ntohs(nexthdrnum); // we need it in host order
    snprintf(editbuf, 64, "%s sets %s.%s=0x%.4x", action_name,
            dstheader->name, dstf->name, nexthdrnum);
    struct ConfAssignment *a = calloc_struct(ConfAssignment);
    a->text = strdup(editbuf);
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
    char editbuf[64];
    const struct Protocol *dstpr = &protocol_list[dstheader->id];
    unsigned dst_idx = dstpr->nexthdr_idx;
    const struct ProtocolField *dstf = &dstpr->header_fields[dst_idx];
    const struct Protocol *srcpr = &protocol_list[srcheader->id];
    unsigned src_idx = srcpr->nexthdr_idx;
    const struct ProtocolField *srcf = &srcpr->header_fields[src_idx];
    if (dstpr->get_nexthdr != srcpr->get_nexthdr) {
        fprintf(stderr, "nexthdr field type mismatch\n");
        return NULL;
    }

    snprintf(editbuf, 64, "%s sets %s.%s=%s.%s", action_name,
            dstheader->name, dstf->name, srcheader->name, srcf->name);
    struct ConfAssignment *a = calloc_struct(ConfAssignment);
    a->text = strdup(editbuf);
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

    switch (stst->actions->type) {
        case CA_UNDEF:
            THROW("no action\n");
        case CA_ADD:
            printf("CA_ADD: %s %s %s %s\n",
                    stst->actions->d.add.beforeafter==ADD_BEFORE?"before":"after",
                    stst->actions->d.add.pos->name,
                    stst->actions->d.add.newname,
                    protocol_type_from_id(stst->actions->d.add.id));
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
                a->lhs.v.header.field->header_idx = pos_idx;
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
            //      edit newheader.seq=meta.seq (writeseq newheader)
            //      TODO unless stst->actions->d.add.assignments already has an assignment for it
            //              what reasonable assignment could it have?
            //TODO if the new header has a FT_TSNTSTAMP field, automatically create
            //      edit newheader.tstamp=meta.tstamp (writetstamp newheader)
            //      TODO unless stst->actions->d.add.assignments already has an assignment for it
            //              what reasonable assignment could it have?

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
                a->next = edit->d.edit.assignments;
                edit->d.edit.assignments = a;
            }

            // set nexthdr of prevheader with newheader's type
            if (prevheader && protocol_list[prevheader->id].get_nexthdr != NULL) {
                struct ConfAssignment *a = assign_nexthdrid_from_header_type("add", prevheader, pos_idx-1, newheader);
                if (a == NULL) {
                    THROW("header type %s cannot have type %s as next header",
                            protocol_type_from_id(prevheader->id),
                            protocol_type_from_id(newheader->id));
                }
                a->next = edit->d.edit.assignments;
                edit->d.edit.assignments = a;
            }

            if (edit->d.edit.assignments == NULL) {
                stst->actions = edit->next;
                free(edit->text);
                free(edit);
            } else {
                process_action(stst); // now edit is the newest action
            }
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
            if (!stst->seq_set) {
                THROW("can't eliminate without a sequence number");
            }
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
                    delete_confaction_list(jstst.actions);
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
            if (stst->actions->d.pof.pof == NULL) {
                THROW("no POF object specified");
            }
            break;
        case CA_READSEQ:
        case CA_WRITESEQ:
            do {
                if (stst->actions->d.meta.hdr == NULL) {
                    THROW("no header specified");
                }
                int hdrtype = stst->actions->d.meta.hdr->id;
                int field_idx = protocol_get_field_id_by_type(hdrtype, FT_TSNSEQ);
                if (field_idx == -1) {
                    THROW("header '%s' doesn't have a sequence number field", stst->actions->d.meta.hdr->name);
                }
                stst->actions->d.meta.field = new_headerfield(
                        header_index(stst->headers, stst->actions->d.meta.hdr),
                        &protocol_list[hdrtype].header_fields[field_idx]);
                if ((stst->actions->d.meta.field->bitoffset % 8)
                        || (stst->actions->d.meta.field->bitcount != 32)) {
                    THROW("sequence number field of header '%s' is invalid", stst->actions->d.meta.hdr->name);
                }
                if (stst->actions->type == CA_READSEQ) {
                    stst->seq_set = true;
                } else {
                    if (!stst->seq_set) {
                        THROW("can't write undefined sequence number");
                    }
                }
            } while (0);
            break;
        case CA_READTSTAMP:
        case CA_WRITETSTAMP:
            //TODO unify this with the SEQ code
            do {
                if (stst->actions->d.meta.hdr == NULL) {
                    THROW("no header specified");
                }
                int hdrtype = stst->actions->d.meta.hdr->id;
                int field_idx = protocol_get_field_id_by_type(hdrtype, FT_TSNTSTAMP);
                if (field_idx == -1) {
                    THROW("header '%s' doesn't have a timestamp field", stst->actions->d.meta.hdr->name);
                }
                stst->actions->d.meta.field = new_headerfield(
                        header_index(stst->headers, stst->actions->d.meta.hdr),
                        &protocol_list[hdrtype].header_fields[field_idx]);
                if ((stst->actions->d.meta.field->bitoffset % 8)
                        || (stst->actions->d.meta.field->bitcount != 32)) {
                    THROW("timestamp field of header '%s' is invalid", stst->actions->d.meta.hdr->name);
                }
            } while (0);
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
                    delete_confaction_list(pstst.actions);
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
        case CA_SEQGEN:
            if (stst->actions->d.seq.gen == NULL) {
                THROW("seqgen needs a sequence generator object");
            }
            stst->seq_set = true;
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

    if (stst->actions->type == CA_UNDEF) {
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
        .seq_set = false,
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
            case CA_UNDEF:
                break;
            case CA_ADD:
                free(del->d.add.newname);
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
            case CA_READSEQ:
            case CA_READTSTAMP:
            case CA_WRITESEQ:
            case CA_WRITETSTAMP:
                free(del->d.meta.field);
                break;
            case CA_REPL:
                delete_replicatelist(del->d.repl.pipelines);
                break;
            case CA_SEND:
                break;
            case CA_SEQGEN:
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
            fprintf(stderr, "assign '%s' destination is undefined\n", l->text);
            free(ret);
            return NULL;
        }
        if (l->lhs.type == CVT_FIELD)
            a->write_state = l->lhs.v.header.field; //TODO memdup

        switch (l->rhs.type) {
            case CVT_UNDEF:
                fprintf(stderr, "assign '%s' source is undefined\n", l->text);
                free(ret);
                return NULL;
            case CVT_FIELD:
                a->read_state = l->rhs.v.header.field; //TODO memdup
                break;
            case CVT_CONST:
                a->constant = l->rhs.value;
                unsigned len = DIVCEIL(a->constant.bitoffset + a->constant.bitcount, 8);
                a->constant.value = memdup(l->rhs.value.value, len);
                a->read = NULL; // this was the dummy_constant_reader
                break;
            case CVT_IFACE:
                a->read_state = l->rhs.v.iface.iface;
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
            case CA_UNDEF:
                fprintf(stderr, "cannot assemble undefined action\n");
                break;
            case CA_ADD:
                create_action_add(ret+a, ca->d.add.pos_idx, ca->d.add.id, ca->d.add.len, ca->text);
                break;
            case CA_DEL:
                create_action_del(ret+a, ca->d.del.idx, ca->text);
                break;
            case CA_DELAY:
                create_action_delay(ret+a, ntohl(*(unsigned *)ca->d.delay.delay_value.value.value), ca->d.delay.timestamp_field, ca->text);
                break;
            case CA_DROP:
                create_action_drop(ret+a, ca->text);
                break;
            case CA_EDIT: {
                unsigned acount;
                struct EditAssign *assigns = assemble_fieldassigns(ca->d.edit.assignments, &acount);
                if (assigns == NULL) {
                    //TODO cleanup on error
                    fprintf(stderr, "could not assemble the field assignments for edit action\n");
                    return NULL;
                }
                create_action_edit(ret+a, assigns, acount, ca->text);
                break; }
            case CA_ELIM:
                create_action_elim(ret+a, ca->d.elim.rec, ca->text);
                break;
            case CA_JUMP:
                fprintf(stderr, "assemble_actions() jump should have been inlined\n");
                //TODO cleanup on error
                return NULL;
            case CA_POF:
                create_action_pof(ret+a, ca->d.pof.pof, ca->text);
                break;
            case CA_READSEQ:
                create_action_readseq(ret+a, ca->d.meta.field, ca->text);
                break;
            case CA_READTSTAMP:
                create_action_readtstamp(ret+a, ca->d.meta.field, ca->text);
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
            case CA_SEQGEN:
                create_action_seqgen(ret+a, ca->d.seq.gen, ca->text);
                break;
            case CA_WRITESEQ:
                create_action_writeseq(ret+a, ca->d.meta.field, ca->text);
                break;
            case CA_WRITETSTAMP:
                create_action_writetstamp(ret+a, ca->d.meta.field, ca->text);
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
            case CA_UNDEF:
                break;
            case CA_ADD:
                printf("  new name %s id %d type %s len %u index %u\n",
                        ca->d.add.newname, ca->d.add.id,
                        protocol_type_from_id(ca->d.add.id),
                        ca->d.add.len, ca->d.add.pos_idx);
                break;
            case CA_DEL:
                printf("  index %u\n", ca->d.del.idx);
                break;
            case CA_DELAY:
                printf("  delaying %u hdr id %d \n", htonl(*(unsigned*)ca->d.delay.delay_value.value.value), ca->d.delay.timestamp_field.header_idx);
                break;
            case CA_DROP:
                break;
            case CA_EDIT:
                for (struct ConfAssignment *a=ca->d.edit.assignments; a; a=a->next) {
                    printf("  %s\n", a->text);
                    printf("      read %p write %p\n", a->read, a->write);
                    printf("    lhs type %s valuetype %s bitoffset %u bitcount %u\n",
                            variabletype_name_from_type(a->lhs.type),
                            fieldtype_name_from_type(a->lhs.value_type),
                            a->lhs.value.bitoffset, a->lhs.value.bitcount);
                    if (a->lhs.type == CVT_FIELD) {
                        printf("      index %u\n", a->lhs.v.header.field->header_idx);
                    }
                    printf("    rhs type %s valuetype %s bitoffset %u bitcount %u\n",
                            variabletype_name_from_type(a->rhs.type),
                            fieldtype_name_from_type(a->rhs.value_type),
                            a->rhs.value.bitoffset, a->rhs.value.bitcount);
                    if (a->rhs.type == CVT_FIELD) {
                        printf("      index %u\n", a->rhs.v.header.field->header_idx);
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
                break;
            case CA_JUMP:
                printf("  %s\n", ca->d.jump.pipename);
                break;
            case CA_POF:
                break;
            case CA_READSEQ:
            case CA_READTSTAMP:
            case CA_WRITESEQ:
            case CA_WRITETSTAMP:
                printf("  field idx %u bitoffset %u bitcount %u\n",
                        ca->d.meta.field->header_idx, ca->d.meta.field->bitoffset, ca->d.meta.field->bitcount);
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
            case CA_SEQGEN:
                break;
        }
    }
}
