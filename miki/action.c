
#include "action.h"
#include "delay.h"
#include "header.h"
#include "interface.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_recov.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *action_name_from_type(enum ActionType type)
{
    switch (type) {
        case ACT_ADD:
            return "Add";
        case ACT_DEL:
            return "Del";
        case ACT_DELAY:
            return "Delay";
        case ACT_DROP:
            return "Drop";
        case ACT_EDIT:
            return "Edit";
        case ACT_ELIM:
            return "Eliminate";
        case ACT_POF:
            return "POF";
        case ACT_REPL:
            return "Replicate";
        case ACT_SEND:
            return "Send";
    }
    return NULL;
}


/////////////////////////////////////////////////////////////////////

struct AddData {
    unsigned idx;
    int type;
    unsigned len;
};

static enum ActionResult action_add_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct AddData *ad = a->action_private;
    packet_add_header(pi->packet, ad->idx, ad->type, ad->len);
    return ACR_CONTINUE;
}

static void action_add_del(void *action_private)
{
    struct AddData *ad = action_private;
    free(ad);
}

void create_action_add(struct Action *a, unsigned idx, int type, unsigned len, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_ADD;
    a->execute = action_add_execute;
    a->del = action_add_del;
    a->text = strdup(text);

    struct AddData *ad = calloc_struct(AddData);
    ad->idx = idx;
    ad->type = type;
    ad->len = len;
    a->action_private = ad;
}

/////////////////////////////////////////////////////////////////////

struct DelData {
    unsigned idx;
};

static enum ActionResult action_del_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct DelData *dd = a->action_private;
    packet_del_header(pi->packet, dd->idx);
    return ACR_CONTINUE;
}

static void action_del_del(void *action_private)
{
    struct DelData *dd = action_private;
    free(dd);
}

void create_action_del(struct Action *a, unsigned idx, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_DEL;
    a->execute = action_del_execute;
    a->del = action_del_del;
    a->text = strdup(text);

    struct DelData *dd = calloc_struct(DelData);
    dd->idx = idx;
    a->action_private = dd;
}

/////////////////////////////////////////////////////////////////////

struct DelayData {
    unsigned delay_ms;
    struct HeaderField timestamp;
};

static enum ActionResult action_delay_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct DelayData *dd = a->action_private;
    delay_insert(pi, dd->delay_ms);
    return ACR_HOLD;
}

static void action_delay_del(void *action_private)
{
    struct DelayData *dd = action_private;
    free(dd);
}

void create_action_delay(struct Action *a, unsigned delay_ms, struct HeaderField *timestamp, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_DELAY;
    a->execute = action_delay_execute;
    a->del = action_delay_del;
    a->text = strdup(text);

    struct DelayData *dd = calloc_struct(DelayData);
    dd->delay_ms = delay_ms;
    dd->timestamp = *timestamp;
    a->action_private = dd;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_drop_execute(struct Action *a, struct PipelineIterator *pi)
{
    (void)a;
    (void)pi;
    return ACR_DONE;
}

void create_action_drop(struct Action *a, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_DROP;
    a->execute = action_drop_execute;
    a->text = strdup(text);
}

/////////////////////////////////////////////////////////////////////

struct EditData {
    struct HeaderFieldAssign *assigns;
    unsigned assign_count;
};

static enum ActionResult action_edit_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct EditData *ed = a->action_private;
    for (unsigned i=0; i<ed->assign_count; i++) {
        if (ed->assigns[i].generator) {
            ed->assigns[i].generator(ed->assigns[i].generator_state,
                    ed->assigns[i].assign, &ed->assigns[i].target,
                    pi->packet);
        } else {
            ed->assigns[i].assign(&ed->assigns[i].target,
                    &ed->assigns[i].constant, pi->packet);
        }
    }
    return ACR_CONTINUE;
}

static void action_edit_del(void *action_private)
{
    struct EditData *ed = action_private;
    for (unsigned i=0; i<ed->assign_count; i++) {
        free(ed->assigns[i].text);
        free(ed->assigns[i].constant.value);
    }
    free(ed->assigns);
    free(ed);
}

void create_action_edit(struct Action *a, struct HeaderFieldAssign *assigns, unsigned assign_count, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_EDIT;
    a->execute = action_edit_execute;
    a->del = action_edit_del;
    a->text = strdup(text);

    struct EditData *ed = calloc_struct(EditData);
    ed->assigns = assigns;
    ed->assign_count = assign_count;
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

struct ElimData {
    struct SequenceRecovery *rcvy;
    value_producer *get_seq;
    void *get_seq_state;
};

static enum ActionResult action_elim_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ElimData *ed = a->action_private;
    if (seq_recovery(ed->rcvy, ed->get_seq, ed->get_seq_state, pi->packet)) {
        return ACR_CONTINUE;
    } else {
        return ACR_DONE;
    }
}


static void action_elim_del(void *action_private)
{
    struct ElimData *ed = action_private;
    free(ed);
}

void create_action_elim(struct Action *a, struct SequenceRecovery *rcvy, value_producer *get_seq, void *get_seq_state, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_ELIM;
    a->execute = action_elim_execute;
    a->del = action_elim_del;
    a->text = strdup(text);

    struct ElimData *ed = calloc_struct(ElimData);
    ed->rcvy = rcvy;
    ed->get_seq = get_seq;
    ed->get_seq_state = get_seq_state;
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

//TODO pof

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_repl_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct PipelineList *list = a->action_private;

    // extract the packet from our iterator
    struct Packet *iterpacket = pi->packet;
    pi->packet = NULL;

    while (list) {
        struct Packet *p;
        if (list->next) {
            p = copy_packet(iterpacket);
        } else {
            p = iterpacket;
        }

        struct PipelineIterator *newpi = new_pipe_iterator(list->pipe, p);
        pipe_iterator_run(newpi);
        list = list->next;
    }
    return ACR_DONE;
}

static void action_repl_del(void *action_private)
{
    struct PipelineList *list = action_private;
    while (list) {
        struct PipelineList *del = list;
        list = list->next;
        pipeline_unref(del->pipe);
        free(del);
    }
}

void create_action_repl(struct Action *a, struct PipelineList *list, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_REPL;
    a->execute = action_repl_execute;
    a->del = action_repl_del;
    a->text = strdup(text);

    a->action_private = list;
}

struct PipelineList *action_repl_get_piplinelist(struct Action *a)
{
    if (a->type == ACT_REPL) {
        struct PipelineList *list = a->action_private;
        return list;
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////

struct SendData {
    struct Interface *iface;
};

static enum ActionResult action_send_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct SendData *sd = a->action_private;
    sd->iface->send(sd->iface, pi->packet);
    return ACR_CONTINUE;
}

static void action_send_del(void *action_private)
{
    struct SendData *sd = action_private;
    free(sd);
}

void create_action_send(struct Action *a, struct Interface *iface, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_SEND;
    a->execute = action_send_execute;
    a->del = action_send_del;
    a->text = strdup(text);

    struct SendData *sd = calloc_struct(SendData);
    sd->iface = iface;
    a->action_private = sd;
}

struct Interface *action_send_get_iface(struct Action *a)
{
    if (a->type == ACT_SEND) {
        struct SendData *sd = a->action_private;
        return sd->iface;
    } else {
        return NULL;
    }
}

/////////////////////////////////////////////////////////////////////

struct Action *delete_action(struct Action *a)
{
    if (!a) return NULL;
    if (a->del)
        a->del(a->action_private);
    free(a->text);
    //free(a); actions are in an array
    return NULL;
}

