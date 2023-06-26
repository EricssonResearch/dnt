// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "action.h"
#include "delay.h"
#include "header.h"
#include "interface.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"
#include "pof.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h> /* htonl() */

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
        case ACT_FILTEROAM:
            return "FilterOAM";
        case ACT_POF:
            return "POF";
        case ACT_READSEQ:
            return "ReadSeq";
        case ACT_READTSTAMP:
            return "ReadTstamp";
        case ACT_REPL:
            return "Replicate";
        case ACT_SEND:
            return "Send";
        case ACT_SEQGEN:
            return "SeqGen";
        case ACT_WRITESEQ:
            return "WriteSeq";
        case ACT_WRITETSTAMP:
            return "WriteTstamp";
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
    //struct HeaderField timestamp;
};

static enum ActionResult action_delay_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct DelayData *dd = a->action_private;
    struct Packet *p = pi->packet;
    unsigned tstamp;
    // get timestamp from metadata
    tstamp = ntohl(p->timestamp) & 0x07FFFFFF;
    //TODO we might not need to delay the packet
    delay_insert(pi, tstamp, dd->delay_ms);
    return ACR_HOLD;
}

static void action_delay_del(void *action_private)
{
    struct DelayData *dd = action_private;
    free(dd);
}

void create_action_delay(struct Action *a, unsigned delay_ms, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_DELAY;
    a->execute = action_delay_execute;
    a->del = action_delay_del;
    a->text = strdup(text);

    struct DelayData *dd = calloc_struct(DelayData);
    dd->delay_ms = delay_ms;
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
    struct EditAssign *assigns;
    unsigned assign_count;
};

static enum ActionResult action_edit_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct EditData *ed = a->action_private;
    for (unsigned i=0; i<ed->assign_count; i++) {
        if (ed->assigns[i].read) {
            ed->assigns[i].read(ed->assigns[i].read_state,
                    ed->assigns[i].write, ed->assigns[i].write_state,
                    pi->packet);
        } else {
            ed->assigns[i].write(ed->assigns[i].write_state,
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
        free(ed->assigns[i].write_state);
        if (ed->assigns[i].owns_read_state)
            free(ed->assigns[i].read_state);
    }
    free(ed->assigns);
    free(ed);
}

void create_action_edit(struct Action *a, struct EditAssign *assigns, unsigned assign_count, const char *text)
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
};

static enum ActionResult action_elim_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ElimData *ed = a->action_private;
    if (seq_recovery(ed->rcvy, pi->packet)) {
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

void create_action_elim(struct Action *a, struct SequenceRecovery *rcvy, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_ELIM;
    a->execute = action_elim_execute;
    a->del = action_elim_del;
    a->text = strdup(text);

    struct ElimData *ed = calloc_struct(ElimData);
    ed->rcvy = rcvy;
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

struct FilterOamData {
    struct HeaderField field;
};

static enum ActionResult action_filteroam_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct FilterOamData *fd = a->action_private;

    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[fd->field.header_idx].start + fd->field.bitoffset/8;
    unsigned len = fd->field.bitcount/8; //TODO this is always 4
    uint32_t seq;
    memcpy(&seq, src, len);

    if (ntohl(seq) & 0x10000000) {
        return ACR_DONE;
    } else {
        return ACR_CONTINUE;
    }
}

static void action_filteroam_del(void *action_private)
{
    struct FilterOamData *fd = action_private;
    free(fd);
}

void create_action_filteroam(struct Action *a, const struct HeaderField *seqfield, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_FILTEROAM;
    a->execute = action_filteroam_execute;
    a->del = action_filteroam_del;
    a->text = strdup(text);

    struct FilterOamData *fd = calloc_struct(FilterOamData);
    fd->field = *seqfield;
    a->action_private = fd;
}

/////////////////////////////////////////////////////////////////////

struct PofData {
    struct Pof *pof;
};

static enum ActionResult action_pof_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct PofData *pd = a->action_private;
    if (pof_insert(pd->pof, pi)) {
        return ACR_HOLD;
    } else {
        return ACR_DONE;
    }
}

static void action_pof_del(void *action_private)
{
    struct PofData *pd = action_private;
    free(pd);
}

void create_action_pof(struct Action *a, struct Pof *pof, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_POF;
    a->execute = action_pof_execute;
    a->del = action_pof_del;
    a->text = strdup(text);

    struct PofData *pd = calloc_struct(PofData);
    pd->pof = pof;
    a->action_private = pd;
}

/////////////////////////////////////////////////////////////////////

struct MetaData {
    struct HeaderField field;
};

static void action_meta_del(void *action_private)
{
    struct MetaData *md = action_private;
    free(md);
}

static enum ActionResult action_readseq_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    memcpy(&p->sequence, src, len);
    return ACR_CONTINUE;
}

void create_action_readseq(struct Action *a, const struct HeaderField *seqfield, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_READSEQ;
    a->execute = action_readseq_execute;
    a->del = action_meta_del;
    a->text = strdup(text);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *seqfield;
    a->action_private = md;
}

static enum ActionResult action_readtstamp_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    memcpy(&p->timestamp, src, len);
    return ACR_CONTINUE;
}

void create_action_readtstamp(struct Action *a, const struct HeaderField *tsfield, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_READTSTAMP;
    a->execute = action_readtstamp_execute;
    a->del = action_meta_del;
    a->text = strdup(text);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *tsfield;
    a->action_private = md;
}

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

struct SeqgenData {
    struct SequenceGenerator *gen;
};

static enum ActionResult action_seqgen_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct SeqgenData *sd = a->action_private;
    seq_generator(sd->gen, pi->packet);
    return ACR_CONTINUE;
}

static void action_seqgen_del(void *action_private)
{
    struct SeqgenData *sd = action_private;
    free(sd);
}

void create_action_seqgen(struct Action *a, struct SequenceGenerator *gen, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_SEQGEN;
    a->execute = action_seqgen_execute;
    a->del = action_seqgen_del;
    a->text = strdup(text);

    struct SeqgenData *sd = calloc_struct(SeqgenData);
    sd->gen = gen;
    a->action_private = sd;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_writeseq_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    memcpy(dst, &p->sequence, len);
    return ACR_CONTINUE;
}

void create_action_writeseq(struct Action *a, const struct HeaderField *seqfield, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_WRITESEQ;
    a->execute = action_writeseq_execute;
    a->del = action_meta_del;
    a->text = strdup(text);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *seqfield;
    a->action_private = md;
}

static enum ActionResult action_writetstamp_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    uint32_t tstamp = htonl( 0x08000000 | (p->recv_time.tv_nsec/1000) | (p->recv_time.tv_sec & 0x00000001) << 20);
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    memcpy(dst, &tstamp, len);
    return ACR_CONTINUE;
}

void create_action_writetstamp(struct Action *a, const struct HeaderField *tsfield, const char *text)
{
    bzero(a, sizeof(*a));
    a->type = ACT_WRITETSTAMP;
    a->execute = action_writetstamp_execute;
    a->del = action_meta_del;
    a->text = strdup(text);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *tsfield;
    a->action_private = md;
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
