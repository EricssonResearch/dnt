
#include "conf_actions.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "action.h"
#include "interface.h"
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
            struct StringList *assignments;
        } add;
        struct {
            char *pipename;
            struct StringList *replacements;
        } call;
        struct {
            struct ConfHeader *hdr;
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
            //TODO recovery object
            //TODO seq field
        } elim;
        struct {
            //TODO pof object
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
    //TODO more stuff that process_actions() receives
};


static bool process_token(char *token, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->actions->type) {
        // here we remember the parameters for the action in the struct
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
                    stst->actions->d.add.pos = pos;
                    //TODO convert "before header" to "after previousheader" ?
                } else if (stst->actions->d.add.newname == NULL) {
                    stst->actions->d.add.newname = strdup(token);
                    char *type = header_type_from_name(token);
                    stst->actions->d.add.id = protocol_id_from_type(type);
                    if (stst->actions->d.add.id < 0) {
                        //TODO throw exception: type is invalid for new header
                    }
                    stst->actions->d.add.newtype = type;
                } else {
                    stringlist_push(&stst->actions->d.add.assignments, strdup(token));
                }
                break;
            case CA_CALL:
                if (stst->actions->d.call.pipename == NULL) {
                    stst->actions->d.call.pipename = strdup(token);
                } else {
                    stringlist_push(&stst->actions->d.call.replacements, strdup(token));
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
                //TODO first argument is a recovery object
                //TODO second argument is sequence field
                break;
            case CA_POF:
                //TODO first arguments is pof object
                //TODO second argument is sequence field
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
            //TODO see if token is the name of one of the objects
            //TODO if not, throw exception: unknown action
            //TODO if yes,
            //  stst->actions->type = from the object;
            //  stst->actions->d.pof.obj = object
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
            newheader->type = stst->actions->d.add.newtype;
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
            edit = calloc_struct(ConfAction);
            edit->type = CA_EDIT;
            edit->text = strdup("add sets nexthdr"); //TODO more informative
            edit->d.edit.hdr = newheader;
            const char *nexthdrfield = protocol_list[newheader->id].nexthdr;
            uint16_t nexthdrnum = ntohs(protocol_list[newheader->id].get_nexthdr(nextheader->id));
            char editbuf[64];
            snprintf(editbuf, 64, "%s=%d", nexthdrfield, nexthdrnum);
            stringlist_push(&edit->d.edit.assignments, strdup(editbuf));
            edit->next = stst->actions;
            stst->actions = edit;
            process_action(stst); // now edit is the newest action

            // set nexthdr for prevheader
            if (prevheader) {
                edit = calloc_struct(ConfAction);
                edit->type = CA_EDIT;
                edit->text = strdup("add sets nexthdr"); //TODO more informative
                edit->d.edit.hdr = prevheader;
                nexthdrfield = protocol_list[prevheader->id].nexthdr;
                nexthdrnum = ntohs(protocol_list[prevheader->id].get_nexthdr(newheader->id));
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
            //TODO get the key value from the config section
            //TODO do the %substitutions% on the value
            //TODO call foreach_stages() on the value
            //TODO insert the returned action chain in place of this one
            break;
        case CA_DEL:
            if (stst->actions->d.del.hdr == NULL) {
                //TODO throw exception: no header to delete
            }
            if (stst->actions->d.del.hdr->state == CH_DEL) {
                //TODO throw exception: already deleted
            }
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
                //TODO process the field assignments here or already in process_token()?
                //      here, now we know the index
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

struct Action *process_actions(const char *stream, char *line, struct ConfHeader *headers,
        struct Interface *ifaces, unsigned ifcount,
        unsigned *action_count)
{
    struct StageState stst = {0};
    stst.stream = stream;
    stst.headers = headers;
    stst.ifaces = ifaces;
    stst.ifcount = ifcount;
    foreach_stages(line, process_stage, &stst);

    //TODO now we should have a linked list of processed actions in stst
    //TODO reverse the list

    //TODO compile the linked list into an Action array

    *action_count = 0;
    return NULL;
}

