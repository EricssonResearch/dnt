
#include "conf_actions.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum ConfActionType {
    CA_ADD = 1,
    CA_CALL,
    CA_DEL,
    CA_DELAY,
    CA_DROP,
    CA_EDIT,
    CA_ELIM,
    CA_OBJ,
    //CA_POF,
    CA_REPL,
    CA_SEND,
};

struct ConfAction {
    enum ConfActionType type;
    char *text;
    struct ConfAction *next;
    // action-specific data
    char *name;
    char *pos;
    int beforeafter; // 1: before, 2: after
    //TODO we need a string list
};

struct StageState {
    const char *stream;
    struct ConfAction *actions;
    struct ConfHeader *headers;
    //TODO more stuff that process_actions() receives
};


static bool process_token(char *token, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->actions->type) {
        // here we remember the parameters for the action in the struct
        switch (stst->actions->type) {
            case CA_ADD:
                if (stst->actions->beforeafter == 0) {
                    if (strcmp(token, "before") == 0)
                        stst->actions->beforeafter = 1;
                    else if (strcmp(token, "after") == 0)
                        stst->actions->beforeafter = 2;
                    else {
                        //TODO throw exception
                    }
                } else if (stst->actions->pos == NULL) {
                    //TODO check if token is a header in the stst->headers list
                    //  also, remember the pointr to that header
                    stst->actions->pos = strdup(token);
                } else if (stst->actions->name == NULL) {
                    //TODO check if token is a valid header name
                    stst->actions->name = strdup(token);
                } else {
                    //TODO the rest of the tokens should be field assignments
                }
                break;
            case CA_CALL:
                if (stst->actions->name == NULL) {
                    stst->actions->name = strdup(token);
                } else {
                    //TODO the rest of the tokens should be value assignments
                }
                break;
            case CA_DEL:
                if (stst->actions->name) {
                    //TODO throw exception
                }
                //TODO check if token is a header in the stst->headers list
                //  also, remember the pointr to that header
                stst->actions->name = strdup(token);
                break;
            case CA_DELAY:
                //TODO first argument is timestamp field
                //TODO second argument is a delay value
                break;
            case CA_DROP:
                //TODO no parameter expected -> throw exception
                break;
            case CA_EDIT:
                if (stst->actions->name == NULL) {
                    //TODO check if token is a valid header name
                    stst->actions->name = strdup(token);
                } else {
                    //TODO the rest of the tokens should be field assignments
                }
                break;
            case CA_ELIM:
                //TODO first argument is sequence field
                //TODO second argument is a recovery object
                break;
            case CA_OBJ:
                //TODO how do we collect generic parameters?
                break;
            case CA_REPL:
                //TODO arguments are pipeline names (collect them in a list)
                break;
            case CA_SEND:
                if (stst->actions->name) {
                    //TODO throw exception
                }
                //TODO validate that token is the name of an existing interface
                stst->actions->name = strdup(token);
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
        } else if (strcmp(token, "replicate") == 0) {
            stst->actions->type = CA_REPL;
        } else if (strcmp(token, "send") == 0) {
            stst->actions->type = CA_SEND;
        } else {
            //TODO see if token is one of the objects
            //TODO if not, throw exception: unknown action
            //TODO if yes, stst->actions->type = CA_OBJ and remember the name
        }
    }

    return true;
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

    //TODO now we have all arguments for the action, let's process it
    switch (stst->actions->type) {
        default:
            break;
    }

    return true;
}

struct Action *process_actions(const char *stream, char *line, struct ConfHeader *headers, unsigned *action_count)
{
    struct StageState stst = {0};
    stst.stream = stream;
    stst.headers = headers;
    foreach_stages(line, process_stage, &stst);

    //TODO now we should have a linked list of processed actions in stst
    //TODO reverse the list

    //TODO compile the linked list into an Action array

    *action_count = 0;
    return NULL;
}

