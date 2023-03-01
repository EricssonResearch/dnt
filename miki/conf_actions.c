
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
#include "protocol.h"
#include "transfer.h"
#include "utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <arpa/inet.h> /* htons() */

enum ConfActionType {
    CA_ADD = 1,
    CA_CALL,
    CA_DEL,
    CA_DELAY,
    CA_DROP,
    CA_EDIT,
    CA_ELIM,
    CA_POF,
    CA_REPL,
    CA_SEND,
};

enum BeforeAfter {
    ADD_UNKNOWN,
    ADD_BEFORE,
    ADD_AFTER,
};

struct StringList {
    char *string;
    struct StringList *next;
};

static void stringlist_push_string(struct StringList **list, char *string)
{
    struct StringList *l = calloc_struct(StringList);
    l->string = string;
    l->next = *list;
    *list = l;
}

enum ConfVariableType {
    CT_UNDEF,
    CT_FIELD, // header field
    CT_PACKET, // packet property
    // the ones below are rhs-only
    CT_CONST, // constant
    CT_IFACE, // interface property
    CT_GEN, // generator object
};

struct ConfVariable {
    enum ConfVariableType type;
    struct Value value;
    union {
        struct {
            struct HeaderDescriptor *header;
            struct HeaderField *field;
            value_consumer *write;
            value_producer *read;
        } field;
        struct {
            char *pname;
            enum ProtocolFieldType ptype;
            value_consumer *write;
            value_producer *read;
        } packet;
        struct {
            struct ConfObject *obj;
        } object;
        struct {
            char *iface;
            char *property;
        } iface;
        struct {
            enum ProtocolFieldType ptype;
            struct Value *value; //TODO do we need this? just use ConfVariable::value
        } constant;
    } v;
};

struct ConfAssignment {
    struct ConfVariable lhs;
    struct ConfVariable rhs;
    char *text;
    struct ConfAssignment *next;
};

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
        } add;
        struct {
            char *pipename;
            struct HashMap *replacements;
        } call;
        struct {
            struct HeaderDescriptor *hdr;
            unsigned idx;
        } del;
        struct {
            struct ConfVariable timestamp_field;
            struct ConfVariable delay_value;
        } delay;
        struct {
            struct ConfAssignment *assignments;
        } edit;
        struct {
            struct ConfObject *rec;
            struct ConfVariable seq_field;
        } elim;
        struct {
            struct ConfObject *pof;
            struct ConfVariable seq_field;
        } pof;
        struct {
            char *name;
            struct StringList *pipelines;
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
};

// @returns the position of @pos in the linked list @headers
// @pos must be in the linked list!
// skips the headers marked as CH_DEL
static unsigned header_index(const struct HeaderDescriptor *headers, const struct HeaderDescriptor *pos)
{
    unsigned pos_idx = 0;
    for (const struct HeaderDescriptor *h=headers; h!=pos; h=h->next) {
        if (h->state != CH_DEL) pos_idx++;
    }
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

// @returns false on error
static bool process_assignment_rhs(struct StageState *stst, enum ProtocolFieldType lhstype,
        struct ConfVariable *var, char *rhs)
{
    //TODO in each case set var->value to describe the offset/length of the rhs value!
    char *key, *val;
    if (parse_fieldname(rhs, &key, &val)) {
        //printf("rhs has a dot\n")
        struct HeaderDescriptor *h = header_list_find_by_name(stst->headers, key);
        if (h) {
            struct ProtocolField *f = protocol_get_field_by_name(h->id, val);
            if (f) {
                //TODO rhs is a header field!
                //TODO var->v.field.read = get_read_function()
                return true;
            } else {
                //TODO throw exception: header has no such field
            }
        }

        struct Interface *iface = find_interface(stst, key);
        if (iface) {
            //TODO rhs is an interface!
            //TODO check that it has a property named @val
        }

        if (strcmp(key, "packet") == 0) {
            printf("rhs is a packet property!\n");
            enum ProtocolFieldType rhstype = packet_get_property_type(val);
            if (lhstype != rhstype) {
                //TODO throw exception
            }
            var->type = CT_PACKET;
            var->v.packet.pname = strdup(val);
            var->v.packet.ptype = rhstype;
            var->value.bitoffset = 0;
            var->value.bitcount = 32;
            return true;
        }

        // if nothing matched, restore the dot
        val[-1] = '.';
    }

    struct ConfObject *obj = hashmap_find(stst->objects, key);
    if (obj) {
        printf("rhs is a value generator object!\n");
        // we only have one generator type: seqgen
        // TODO how do we handle the timestamp generator?
        if (obj->type != CO_SEQGEN) {
            //TODO throw exception
        }
        if (lhstype != FT_TSNSEQ) {
            //TODO throw exception
        }
        var->type = CT_GEN;
        var->v.object.obj = obj;
        var->value.bitoffset = 0;
        var->value.bitcount = 32;
        return true;
    }

    //TODO rhs must be a constant
    //TODO put this into a function, reuse in process_packet_line()
    var->type = CT_CONST;
    switch (lhstype) {
        case FT_UNKNOWN:
            //TODO wtf?
            break;
        case FT_NUMBER: {
            unsigned num;
            if (sscanf(rhs, "%i%*c", &num) != 1) {
                //TODO throw exception
            }
            var->v.constant.ptype = FT_NUMBER;
            //TODO construct var->v.constant.value
            //      TODO adjust bitoffset to match lhs
            //      TODO check that num fits into the bitcount of lhs
            //TODO fill var->value
            break; }
        case FT_MACADDRESS:
            //TODO eth_aton()
            break;
        case FT_IPV4ADDRESS:
            //TODO inet_aton()
            break;
        case FT_IPV6ADDRESS:
            break;
        case FT_TSNSEQ:
            //TODO when do we set seq from a constant?
            break;
        case FT_TSNTSTAMP:
            //TODO when do we set tstamp from a constant?
            break;
    }

    return false;
}

static bool process_token(char *token, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->actions->type) {
        // here we process the parameters for the action individually
        switch (stst->actions->type) {
            case CA_ADD:
                if (stst->actions->d.add.beforeafter == ADD_UNKNOWN) {
                    if (strcmp(token, "before") == 0)
                        stst->actions->d.add.beforeafter = ADD_BEFORE;
                    else if (strcmp(token, "after") == 0)
                        stst->actions->d.add.beforeafter = ADD_AFTER;
                    else {
                        //TODO throw exception: invalid location designator
                    }
                } else if (stst->actions->d.add.pos == NULL) {
                    struct HeaderDescriptor *pos = header_list_find_by_name(stst->headers, token);
                    if (pos == NULL) {
                        //TODO throw exception: no such header in the packet
                    }
                    if (pos->state == CH_DEL) {
                        //TODO throw exception: can't set position with deleted header
                    }
                    stst->actions->d.add.pos = pos;
                } else if (stst->actions->d.add.newname == NULL) {
                    stst->actions->d.add.newname = strdup(token);
                    char *type = header_type_from_name(token);
                    stst->actions->d.add.id = protocol_id_from_type(type);
                    if (stst->actions->d.add.id < 0) {
                        //TODO throw exception: type is invalid for new header
                    }
                    stst->actions->d.add.newtype = type;
                    stst->actions->d.add.len = protocol_list[stst->actions->d.add.id].bytelength;
                } else {
                    //TODO parse assignment using the already known header name/type/id
                    //TODO use process_assignment_rhs()
                }
                break;
            case CA_CALL:
                if (stst->actions->d.call.pipename == NULL) {
                    stst->actions->d.call.pipename = strdup(token);
                } else {
                    if (stst->actions->d.call.replacements == NULL)
                        stst->actions->d.call.replacements = new_hashmap(13, NULL, NULL);
                    char *key, *val;
                    if (parse_assignment(token, &key, &val)) {
                        //TODO val should not contain '='
                        hashmap_insert(stst->actions->d.call.replacements, strdup(key), strdup(val));
                    } else {
                        //TODO throw exception: invalid replacement
                    }
                }
                break;
            case CA_DEL:
                if (stst->actions->d.del.hdr) {
                    //TODO throw exception: delete action takes only 1 parameter
                } else {
                    struct HeaderDescriptor *del = header_list_find_by_name(stst->headers, token);
                    if (del) {
                        stst->actions->d.del.hdr = del;
                    } else {
                        //TODO throw exception: invalid header name
                    }
                }
                break;
            case CA_DELAY:
                //TODO first argument is timestamp field
                //      TODO we need a parser for headername.fieldname
                //      TODO validate headername and fieldname
                //TODO second argument is a delay value (time)
                break;
            case CA_DROP:
                //TODO no parameter expected -> throw exception
                break;
            case CA_EDIT: {
                char *lhs, *rhs;
                struct ConfAssignment *a = calloc_struct(ConfAssignment);
                a->next = stst->actions->d.edit.assignments;
                stst->actions->d.edit.assignments = a;
                a->text = strdup(token);
                if (parse_assignment(token, &lhs, &rhs)) {
                    // process lhs
                    char *hdr, *field;
                    enum ProtocolFieldType lhstype = FT_UNKNOWN;
                    if (parse_fieldname(lhs, &hdr, &field)) {
                        if (strcmp(hdr, "packet") == 0) {
                            a->lhs.type = CT_PACKET;
                            lhstype = packet_get_property_type(field);
                            if (lhstype == FT_UNKNOWN) {
                                //TODO throw exception: no such packet property
                            }
                            a->lhs.v.packet.pname = strdup(field);
                            a->lhs.v.packet.ptype = lhstype;
                        } else {
                            struct HeaderDescriptor *h = header_list_find_by_name(stst->headers, hdr);
                            if (h == NULL) {
                                //TODO throw exception: no such header in the packet
                            }
                            struct ProtocolField *f = protocol_get_field_by_name(h->id, field);
                            if (f == NULL) {
                                //TODO throw exception: header has no such field
                            }
                            struct HeaderField *hf = calloc_struct(HeaderField);
                            hf->header_idx = header_index(stst->headers, h);
                            hf->bitoffset = f->bitoffset;
                            hf->bitcount = f->bitcount;
                            a->lhs.type = CT_FIELD;
                            a->lhs.v.field.header = h;
                            a->lhs.v.field.field = hf;
                            lhstype = f->type;
                        }
                    } else {
                        //TODO throw exception: invalid lhs
                    }

                    // process rhs
                    if (!process_assignment_rhs(stst, lhstype, &a->rhs, rhs)) {
                        //TODO throw exception: invalid rhs
                    }

                    // select consumer function for lhs
                    if (a->lhs.type == CT_PACKET) {
                        a->lhs.v.packet.write = packet_get_property_writer(field, &a->rhs.value);
                        if (a->lhs.v.packet.write == NULL) {
                            //TODO throw exception: packet property cannot be written from this rhs
                        }
                    } else {
                        a->lhs.v.field.write = get_assign_function(a->lhs.v.field.field, &a->rhs.value);
                        if (a->lhs.v.field.write == NULL) {
                            //TODO throw exception: header field cannot be written from this rhs
                        }
                    }
                } else {
                    //TODO throw exception: invalid assignment
                }
                break; }
            case CA_ELIM:
                if (stst->actions->d.elim.rec == NULL) {
                    struct ConfObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        if (obj->type == CO_SEQREC) {
                            stst->actions->d.elim.rec = obj;
                        } else {
                            //TODO throw exception: elim first argument must be a recovery object
                        }
                    } else {
                        //TODO throw exception: elim first argument must be a recovery object
                    }
                } else {
                    //TODO second argument is sequence field
                }
                break;
            case CA_POF:
                if (stst->actions->d.pof.pof == NULL) {
                    struct ConfObject *obj = hashmap_find(stst->objects, token);
                    if (obj) {
                        if (obj->type == CO_POF) {
                            stst->actions->d.pof.pof = obj;
                        } else {
                            //TODO throw exception: pof first argument must be a pof object
                        }
                    } else {
                        //TODO throw exception: pof first argument must be a pof object
                    }
                } else {
                    //TODO second argument is sequence field
                }
                break;
            case CA_REPL:
                stringlist_push_string(&stst->actions->d.repl.pipelines, strdup(token));
                break;
            case CA_SEND:
                if (stst->actions->d.send.iface) {
                    //TODO throw exception: can only send on one interface
                } else {
                    struct Interface *iface = find_interface(stst, token);
                    if (iface == NULL) {
                        //TODO throw exception: unknown interface
                    }
                    //TODO dynconf: defer finalizing the iface pointer to assemble_actions()
                    stst->actions->d.send.iface = iface;
                }
                break;
        }
    } else {
        if        (strcmp(token, "add") == 0) {
            stst->actions->type = CA_ADD;
        } else if (strcmp(token, "call") == 0) {
            stst->actions->type = CA_CALL;
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
                        //TODO throw exception: gen cannot be used as action
                        break;
                    case CO_SEQREC:
                        stst->actions->type = CA_ELIM;
                        stst->actions->d.elim.rec = obj;
                        break;
                    case CO_POF:
                        stst->actions->type = CA_POF;
                        stst->actions->d.pof.pof = obj;
                        break;
                }
            } else {
                //TODO throw exception: action name invalid
            }
        }
    }

    return true;
}

static void process_action(struct StageState *stst)
{
    // here we do processing that needs all the parameters of the action
    switch (stst->actions->type) {
        case CA_ADD:
            if (stst->actions->d.add.newname == NULL) {
                //TODO throw exception: no new header name
            }
            if (stst->actions->d.add.pos == NULL) {
                //TODO throw exception: no existing header
            }
            if (stst->actions->d.add.beforeafter == ADD_UNKNOWN) {
                //TODO throw exception: no header position
            }
            // add the newly created header to the header list
            struct HeaderDescriptor *newheader = calloc_struct(HeaderDescriptor);
            newheader->type = stst->actions->d.add.newtype; //TODO strdup?
            newheader->name = stst->actions->d.add.newname;
            newheader->id = stst->actions->d.add.id;
            newheader->state = CH_NEW;
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
            //TODO if "add after last header" then nextheader=NULL
            //      how can we handle this? we only know the payload type at runtime
            //      newheader.tpid = get_nexthdr(get_id(prevhdr.tpid))
            //      prevhdr.tpid = get_nexthdr(newhdr->id)
            //      TODO how can we assemble such a code?
            //      TODO what if prevhdr and nexthdr are both NULL? is it legal to have empty :packet line? no
            //      TODO if (prevhdr->get_id == newhdr->get_id) we can simply copy
            //          TODO if we cannot simply copy -> throw exception
            //      TODO what if we can't set tpid?
            //          e.g. when adding an ip header between vlan tags

            unsigned pos_idx = header_index(stst->headers, stst->actions->d.add.pos);
            if (stst->actions->d.add.beforeafter == ADD_AFTER) pos_idx++;
            stst->actions->d.add.pos_idx = pos_idx;

            newheader->next = nextheader;
            if (prevheader) {
                prevheader->next = newheader;
            } else {
                stst->headers = newheader;
            }

            //TODO merge these three into one edit

            // split off the header assignments into a new edit action
            struct ConfAction *edit = calloc_struct(ConfAction);
            edit->type = CA_EDIT;
            edit->text = strdup(stst->actions->text); //TODO is this okay?
            //edit->d.edit.hdr = newheader;
            //edit->d.edit.assignments = stst->actions->d.add.assignments;
            edit->next = stst->actions;
            stst->actions = edit;
            process_action(stst); // now edit is the newest action

            // set nexthdr for newheader
            char editbuf[64];
            if (protocol_list[newheader->id].get_nexthdr != NULL) {
                struct Protocol *pr = &protocol_list[newheader->id];
                unsigned nexthdrfield = pr->nexthdr_idx;
                uint16_t nexthdrnum;
                if (!pr->get_nexthdr(&nexthdrnum, nextheader->id)) {
                    //TODO throw exception
                }
                snprintf(editbuf, 64, "add sets %s.%s=0x%.4x",
                        newheader->name, pr->header_fields[nexthdrfield].name, ntohs(nexthdrnum));
                edit = calloc_struct(ConfAction);
                edit->type = CA_EDIT;
                edit->text = strdup(editbuf);
                //edit->d.edit.hdr = newheader;
                //TODO stringlist_push_string(&edit->d.edit.assignments, strdup(editbuf));
                edit->next = stst->actions;
                stst->actions = edit;
                process_action(stst); // now edit is the newest action
            }

            // set nexthdr for prevheader
            if (prevheader && protocol_list[prevheader->id].get_nexthdr != NULL) {
                struct Protocol *pr = &protocol_list[prevheader->id];
                unsigned nexthdrfield = pr->nexthdr_idx;
                uint16_t nexthdrnum;
                if (!pr->get_nexthdr(&nexthdrnum, newheader->id)) {
                    //TODO throw exception
                }
                snprintf(editbuf, 64, "add sets %s.%s=0x%.4x",
                        prevheader->name, pr->header_fields[nexthdrfield].name, ntohs(nexthdrnum));
                edit = calloc_struct(ConfAction);
                edit->type = CA_EDIT;
                edit->text = strdup(editbuf);
                //edit->d.edit.hdr = prevheader;
                //TODO stringlist_push_string(&edit->d.edit.assignments, strdup(editbuf));
                edit->next = stst->actions;
                stst->actions = edit;
                process_action(stst); // now edit is the newest action
            }
            break;
        case CA_CALL:
            if (stst->actions->d.call.pipename == NULL) {
                //TODO throw exception: no actionlist
            }
            char *pipestring = inisection_get(stst->streams_sec, stst->actions->d.call.pipename);
            if (pipestring) {
                //TODO do the {key} substitutions on pipestring
                //      we must make a copy because it's const!
                //TODO call foreach_stages() on the value
                //TODO insert the returned action chain in place of this one
                //      warning: the chains are in reverese order
            } else {
                //TODO throw exception: actionlist not found
            }
            break;
        case CA_DEL:
            if (stst->actions->d.del.hdr == NULL) {
                //TODO throw exception: no header to delete
            }
            if (stst->actions->d.del.hdr->state == CH_DEL) {
                //TODO throw exception: already deleted
            }
            //TODO compute idx the same way as in add
            stst->actions->d.del.hdr->state = CH_DEL;
            break;
        case CA_DELAY:
            //TODO check that first param was a valid timestamp field
            //      need to find the header by name
            //TODO check that second param was a valid time constant
            break;
        case CA_DROP:
            // nothing to do here
            break;
        case CA_EDIT:
            if (stst->actions->d.edit.assignments != NULL) {
                //TODO reverse the list

                //TODO compile them into HeaderFieldAssign array? no do that in assemble_actions()
            } else {
                //TODO throw exception: no assignments in edit
            }
            break;
        case CA_ELIM:
            //TODO
            break;
        case CA_POF:
            //TODO
            break;
        case CA_REPL:
            //TODO arguments are action pipeline names, find them in the streams section
            //TODO process all strings with foreach_stages(), remember the resulting linked lists
            break;
        case CA_SEND:
            if (stst->actions->d.send.iface == NULL) {
                //TODO throw exception: no interface to send on
            }
            break;
    }
}

static bool process_stage(char *stage, void *userdata)
{
    struct StageState *stst = userdata;
    struct ConfAction *newaction = calloc_struct(ConfAction);
    newaction->next = stst->actions;
    stst->actions = newaction;
    newaction->text = strdup(stage);

    foreach_tokens(stage, process_token, stst);

    if (stst->actions->type == 0) {
        //TODO throw exception: no action in stage
    }

    // now we have all arguments for the action, let's process it properly
    process_action(stst);

    return true;
}

static struct ConfAction *action_list_pop(struct ConfAction **list)
{
    struct ConfAction *ret = *list;
    *list = (*list)->next;
    ret->next = NULL;
    return ret;
}

static struct ConfAction *action_list_push(struct ConfAction *list, struct ConfAction *a)
{
    a->next = list;
    return a;
}

//TODO template <class T> reverse_list(T *list)
static struct ConfAction *reverse_action_list(struct ConfAction *list)
{
    struct ConfAction *newlist = NULL;
    while (list) {
        struct ConfAction *a = action_list_pop(&list);
        newlist = action_list_push(newlist, a);
    }
    return newlist;
}

struct ConfAction *process_actions_line(const char *stream, char *line, struct HeaderDescriptor *headers,
        struct Interface *ifaces, unsigned ifcount,
        struct HashMap *objects, struct IniSection *streams_sec)
{
    struct StageState stst = {
        .stream = stream,
        .actions = NULL,
        .headers = headers,
        .ifaces = ifaces,
        .ifcount = ifcount,
        .objects = objects,
        .streams_sec = streams_sec
    };
    foreach_stages(line, process_stage, &stst);
    if (stst.actions == NULL) {
        //TODO error
    }

    stst.actions = reverse_action_list(stst.actions);

    //TODO perform optimization passes on the action list
    //      e.g. merge subsequent Edit actions

    // restore @headers:
    //      remove the CH_NEW elements
    //      set CH_DEL back to CH_PACKET
    if (headers) { //TODO is it legal to have no headers?
        while (headers->state == CH_NEW) {
            struct HeaderDescriptor *del = headers;
            headers = headers->next;
            free(del); // don't try to free the pointer members!
        }
        headers->state = CH_PACKET;
        if (headers) { //TODO is it legal to have no headers?
            struct HeaderDescriptor *h = headers;
            while (h->next) {
                if (h->next->state == CH_NEW) {
                    struct HeaderDescriptor *del = h->next;
                    h->next = del->next;
                    free(del); // don't try to free the pointer members!
                } else {
                    h->next->state = CH_PACKET;
                }
                h = h->next;
            }
        }
    }

    return stst.actions;
}

struct Action *assemble_actions(const struct ConfAction *ca_list, unsigned *action_count)
{
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
            case CA_CALL:
                //TODO error, this should have been inlined
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
            case CA_EDIT:
                //TODO compile the intermediate representation into HeaderFieldAssign array
                //create_action_edit(ret+a, assigns, assign_count, ca->text);
                break;
            case CA_ELIM:
                //TODO create_action_elim()
                break;
            case CA_POF:
                //TODO create_action_pof()
                break;
            case CA_REPL:
                //TODO we must compile the action lists into Action arrays here
                //TODO create_action_repl()
                break;
            case CA_SEND:
                create_action_send(ret+a, ca->d.send.iface, ca->text);
                break;
        }
        a++;
    }

    *action_count = count;
    return ret;
}

static const char *confaction_name_from_type(enum ConfActionType type)
{
    switch (type) {
        case CA_ADD:
            return "Add";
        case CA_CALL:
            return "Call";
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
        case CA_POF:
            return "POF";
        case CA_REPL:
            return "Replicate";
        case CA_SEND:
            return "Send";
    }
    return NULL;
}

void print_actions(const struct ConfAction *ca_list)
{
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) {
        fprintf(stderr, "ConfAction %d %s '%s'\n",
                ca->type, confaction_name_from_type(ca->type), ca->text);
        switch (ca->type) {
        case CA_ADD:
            printf("  new name %s type %s id %d len %u\n"
                   "  position %s %s index %u\n",
                    ca->d.add.newname, ca->d.add.newtype, ca->d.add.id, ca->d.add.len,
                    (ca->d.add.beforeafter==ADD_BEFORE?"before":"after"),
                    ca->d.add.pos->name, ca->d.add.pos_idx);
            break;
        case CA_CALL:
            printf("  \n");
            break;
        case CA_DEL:
            printf("  \n");
            break;
        case CA_DELAY:
            printf("  \n");
            break;
        case CA_DROP:
            printf("  \n");
            break;
        case CA_EDIT:
            printf("  \n");
            break;
        case CA_ELIM:
            printf("  \n");
            break;
        case CA_POF:
            printf("  \n");
            break;
        case CA_REPL:
            printf("  \n");
            break;
        case CA_SEND:
            printf("  \n");
            break;
        }
    }
}
