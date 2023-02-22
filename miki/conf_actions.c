
#include "conf_actions.h"
#include "conf_object.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "action.h"
#include "interface.h"
#include "inifile.h"
#include "protocol.h"
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

static void stringlist_push(struct StringList **list, char *string)
{
    struct StringList *l = calloc_struct(StringList);
    l->string = string;
    l->next = *list;
    *list = l;
}

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
            struct ConfHeader *pos; // relative to this header
            enum BeforeAfter beforeafter;
            unsigned pos_idx;
            size_t len;
            struct StringList *assignments;
        } add;
        struct {
            char *pipename;
            struct HashMap *replacements;
        } call;
        struct {
            struct ConfHeader *hdr;
            unsigned idx;
        } del;
        struct {
            //TODO timestamp field
            //TODO delay value
        } delay;
        struct {
            struct ConfHeader *hdr;
            struct StringList *assignments;
        } edit;
        struct {
            struct ConfObject *rec;
            //TODO seq field
        } elim;
        struct {
            struct ConfObject *pof;
            //TODO seq field
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
    struct ConfHeader *headers;
    struct Interface *ifaces;
    unsigned ifcount;
    struct HashMap *objects;
    struct IniSection *streams_sec;
};


static bool process_token(char *token, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->actions->type) {
        // here we validate and remember the parameters for the action
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
                    struct ConfHeader *pos = header_list_find_name(stst->headers, token);
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
                    stringlist_push(&stst->actions->d.add.assignments, strdup(token));
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
                    struct ConfHeader *del = header_list_find_name(stst->headers, token);
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
            case CA_EDIT:
                if (stst->actions->d.edit.hdr == NULL) {
                    struct ConfHeader *edit = header_list_find_name(stst->headers, token);
                    if (edit) {
                        stst->actions->d.edit.hdr = edit;
                    } else {
                        //TODO throw exception: invalid header name
                    }
                } else {
                    //TODO process the assignment here?
                    stringlist_push(&stst->actions->d.edit.assignments, strdup(token));
                }
                break;
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
                stringlist_push(&stst->actions->d.repl.pipelines, strdup(token));
                break;
            case CA_SEND:
                if (stst->actions->d.send.iface) {
                    //TODO throw exception: can only send on one interface
                } else {
                    struct Interface *iface = NULL;
                    for (unsigned i=0; i<stst->ifcount; i++) {
                        if (strcmp(stst->ifaces[i].name, token) == 0)
                            iface = stst->ifaces + i;
                    }
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
            struct ConfHeader *newheader = calloc_struct(ConfHeader);
            newheader->type = stst->actions->d.add.newtype; //TODO strdup?
            newheader->name = stst->actions->d.add.newname;
            newheader->id = stst->actions->d.add.id;
            newheader->state = CH_NEW;
            // add newheader to stst->headers at the designated position
            struct ConfHeader *prevheader, *nextheader;
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
            newheader->next = nextheader;
            if (prevheader) {
                prevheader->next = newheader;
            } else {
                stst->headers = newheader;
            }

            unsigned pos_idx = 0;
            for (struct ConfHeader *ch=stst->headers; ch!=stst->actions->d.add.pos; ch=ch->next)
                if (ch->state != CH_DEL) pos_idx++;
            if (stst->actions->d.add.beforeafter == ADD_AFTER) pos_idx++;
            stst->actions->d.add.pos_idx = pos_idx;

            // split off the header assignments into a new edit action
            struct ConfAction *edit = calloc_struct(ConfAction);
            edit->type = CA_EDIT;
            edit->text = strdup(stst->actions->text); //TODO is this okay?
            edit->d.edit.hdr = newheader;
            edit->d.edit.assignments = stst->actions->d.add.assignments;
            edit->next = stst->actions;
            stst->actions = edit;
            process_action(stst); // now edit is the newest action

            // set nexthdr for newheader
            char editbuf[64];
            if (protocol_list[newheader->id].nexthdr != NULL) {
                edit = calloc_struct(ConfAction);
                edit->type = CA_EDIT;
                edit->text = strdup("add sets nexthdr"); //TODO more informative
                edit->d.edit.hdr = newheader;
                const char *nexthdrfield = protocol_list[newheader->id].nexthdr;
                uint16_t nexthdrnum = ntohs(protocol_list[newheader->id].get_nexthdr(nextheader->id));
                snprintf(editbuf, 64, "%s=%d", nexthdrfield, nexthdrnum);
                stringlist_push(&edit->d.edit.assignments, strdup(editbuf));
                edit->next = stst->actions;
                stst->actions = edit;
                process_action(stst); // now edit is the newest action
            }

            // set nexthdr for prevheader
            if (prevheader && protocol_list[prevheader->id].nexthdr != NULL) {
                edit = calloc_struct(ConfAction);
                edit->type = CA_EDIT;
                edit->text = strdup("add sets nexthdr"); //TODO more informative
                edit->d.edit.hdr = prevheader;
                const char *nexthdrfield = protocol_list[prevheader->id].nexthdr;
                uint16_t nexthdrnum = ntohs(protocol_list[prevheader->id].get_nexthdr(newheader->id));
                snprintf(editbuf, 64, "%s=%d", nexthdrfield, nexthdrnum);
                stringlist_push(&edit->d.edit.assignments, strdup(editbuf));
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
            //TODO check that second param was a valid time constant
            break;
        case CA_DROP:
            // nothing to do here
            break;
        case CA_EDIT:
            if (stst->actions->d.edit.hdr) {
                // find the index of the header that we edit
                unsigned idx = 0;
                struct ConfHeader *h = stst->headers;
                while (h) {
                    if (h == stst->actions->d.edit.hdr) {
                        break;
                    }
                    if (h->state != CH_DEL) idx++;
                    h = h->next;
                }
                //TODO process the field assignments here: prepare constants, find source objects etc.
                //TODO compile them into HeaderFieldAssign array? no do that in assemble_actions()
            } else {
                //TODO throw exception: no header to edit
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

    foreach_tokens(stage, process_token, &stst);

    if (stst->actions->type == 0) {
        //TODO throw exception: no action in stage
    }

    // now we have all arguments for the action, let's process it properly
    process_action(stst);

    return true;
}

struct ConfAction *process_actions(const char *stream, char *line, struct ConfHeader *headers,
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

    //TODO now we have a linked list of processed actions in stst
    //TODO reverse the list
    //TODO perform optimization passes
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

void print_actions(const struct ConfAction *ca_list)
{
    for (const struct ConfAction *ca = ca_list; ca; ca=ca->next) {
        fprintf(stderr, "ConfAction %d\n", ca->type); //TODO
    }
}
