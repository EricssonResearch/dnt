// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "action.h"
#include "object.h"
#include "delay.h"
#include "header.h"
#include "interface.h"
#include "oam.h"
#include "packet.h"
#include "pipeline.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"
#include "pof.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h> /* htonl() */

DEFAULT_LOGGING_MODULE(PIPELINE, WARNING);

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
        case ACT_MEPSTART:
            return "MEPStart";
        case ACT_MEPSTOP:
            return "MEPStop";
        case ACT_MIP:
            return "MIP";
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
        case ACT_TTLCHECK:
            return "TTLCheck";
        case ACT_TTLREDUCE:
            return "TTLReduce";
        case ACT_WRITESEQ:
            return "WriteSeq";
        case ACT_WRITETSTAMP:
            return "WriteTstamp";
    }
    return NULL;
}

#define INIT_ACTION(type_)                      \
    bzero(a, sizeof(*a));                       \
    a->type = ACT_ ## type_;                    \
    a->execute = action_ ## type_ ## _execute;  \
    a->text = strdup(text)

/////////////////////////////////////////////////////////////////////

struct AddData {
    unsigned idx;
    enum ProtocolID type;
    unsigned len;
};

static enum ActionResult action_ADD_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct AddData *ad = (struct AddData *)a->action_private;
    packet_add_header(pi->packet, ad->idx, ad->type, ad->len);
    return ACR_CONTINUE;
}

void create_action_add(struct Action *a, unsigned idx, enum ProtocolID type, unsigned len, const char *text)
{
    INIT_ACTION(ADD);

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

static enum ActionResult action_DEL_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct DelData *dd = (struct DelData *)a->action_private;
    packet_del_header(pi->packet, dd->idx);
    return ACR_CONTINUE;
}

void create_action_del(struct Action *a, unsigned idx, const char *text)
{
    INIT_ACTION(DEL);

    struct DelData *dd = calloc_struct(DelData);
    dd->idx = idx;
    a->action_private = dd;
}

/////////////////////////////////////////////////////////////////////

struct DelayData {
    struct timespec delay;
    bool offload;
    //struct HeaderField timestamp;
};

static enum ActionResult action_DELAY_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct DelayData *dd = (struct DelayData *)a->action_private;
    struct Packet *p = pi->packet;

    // put offload and delay information into the packet
    if (dd->offload) {
        p->offload = true;
        p->delay = dd->delay;
        return ACR_CONTINUE;
    }

    //TODO we might not need to delay the packet
    delay_insert(pi, p->timestamp, dd->delay);
    return ACR_HOLD;
}

void create_action_delay(struct Action *a, const struct timespec delay, bool offload, const char *text)
{
    INIT_ACTION(DELAY);

    struct DelayData *dd = calloc_struct(DelayData);
    dd->delay = delay;
    dd->offload = offload;
    a->action_private = dd;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_DROP_execute(struct Action *a, struct PipelineIterator *pi)
{
    (void)a;
    //struct Packet *p = pi->packet;
    packet_printlog(pi->packet);
    return ACR_DONE;
}

void create_action_drop(struct Action *a, const char *text)
{
    INIT_ACTION(DROP);
}

/////////////////////////////////////////////////////////////////////

struct EditData {
    struct EditAssign *assigns;
    unsigned assign_count;
};

static enum ActionResult action_EDIT_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct EditData *ed = (struct EditData *)a->action_private;
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

static void action_EDIT_del(void *action_private)
{
    struct EditData *ed = (struct EditData *)action_private;
    for (unsigned i=0; i<ed->assign_count; i++) {
        free(ed->assigns[i].text);
        free(ed->assigns[i].constant.value);
        free(ed->assigns[i].write_state);
        if (ed->assigns[i].owns_read_state)
            free(ed->assigns[i].read_state);
    }
    free(ed->assigns);
}

void create_action_edit(struct Action *a, struct EditAssign *assigns, unsigned assign_count, const char *text)
{
    INIT_ACTION(EDIT);
    a->del = action_EDIT_del;

    struct EditData *ed = calloc_struct(EditData);
    ed->assigns = assigns;
    ed->assign_count = assign_count;
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

struct ElimData {
    struct PipelineObject *rcvy;
};

static enum ActionResult action_ELIM_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ElimData *ed = (struct ElimData *)a->action_private;
    return seq_recovery(ed->rcvy, pi);
}

static void action_ELIM_del(void *action_private)
{
    struct ElimData *ed = (struct ElimData *)action_private;
    pipeline_object_unref(ed->rcvy);
}

void create_action_elim(struct Action *a, struct PipelineObject *rcvy, const char *text)
{
    INIT_ACTION(ELIM);
    a->del = action_ELIM_del;

    struct ElimData *ed = calloc_struct(ElimData);
    ed->rcvy = rcvy;
    pipeline_object_ref(rcvy);
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

struct FilterOamData {
    struct HeaderField field;
};

static enum ActionResult action_FILTEROAM_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct FilterOamData *fd = (struct FilterOamData *)a->action_private;

    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[fd->field.header_idx].start + fd->field.bitoffset/8;
    unsigned len = fd->field.bitcount/8; //TODO this is always 4
    uint32_t seq;
    memcpy(&seq, src, len);

    if (SEQ_IS_OAM(seq)) {
        return ACR_DONE;
    } else {
        return ACR_CONTINUE;
    }
}

void create_action_filteroam(struct Action *a, const struct HeaderField *seqfield, const char *text)
{
    INIT_ACTION(FILTEROAM);

    struct FilterOamData *fd = calloc_struct(FilterOamData);
    fd->field = *seqfield;
    a->action_private = fd;
}

/////////////////////////////////////////////////////////////////////

struct PofData {
    struct PipelineObject *pof;
};

static enum ActionResult action_POF_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct PofData *pd = (struct PofData *)a->action_private;
    return pof_insert(pd->pof, pi);
}

static void action_POF_del(void *action_private)
{
    struct PofData *pd = (struct PofData *)action_private;
    pipeline_object_unref(pd->pof);
}

void create_action_pof(struct Action *a, struct PipelineObject *pof, const char *text)
{
    INIT_ACTION(POF);
    a->del = action_POF_del;

    struct PofData *pd = calloc_struct(PofData);
    pd->pof = pof;
    pipeline_object_ref(pof);
    a->action_private = pd;
}

/////////////////////////////////////////////////////////////////////

struct MetaData {
    struct HeaderField field;
};

static enum ActionResult action_READSEQ_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = (struct MetaData *)a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[md->field.header_idx].start + (md->field.bitoffset >> 3);
    unsigned len = md->field.bitcount/8; //TODO this is always 4

    if(p->headers[md->field.header_idx].type == PROTO_ID_IPv6) {
        // this is an SRv6 sequence number, which is only 28 bit
        uint8_t *dst = (uint8_t *)&p->sequence;
        dst[0] = src[0] & 0x0f;      // write indcator bits
        dst[1] = src[1];             // read reserved
        dst[2] = src[2];             // read seqnum 2 bytes
        dst[3] = src[3];
    } else
        memcpy(&p->sequence, src, len);

    return ACR_CONTINUE;
}

void create_action_readseq(struct Action *a, const struct HeaderField *seqfield, const char *text)
{
    INIT_ACTION(READSEQ);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *seqfield;
    a->action_private = md;
}

static enum ActionResult action_READTSTAMP_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = (struct MetaData *)a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    memcpy(&p->timestamp, src, len);
    return ACR_CONTINUE;
}

void create_action_readtstamp(struct Action *a, const struct HeaderField *tsfield, const char *text)
{
    INIT_ACTION(READTSTAMP);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *tsfield;
    a->action_private = md;
}

/////////////////////////////////////////////////////////////////////

struct ReplData {
    struct PipelineList *pipes;
    struct PipelineObject *replobj;
};

static enum ActionResult action_REPL_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ReplData *rd = (struct ReplData *)a->action_private;
    struct PipelineList *list = rd->pipes;

    if (rd->replobj) {
        list = replicate_get_pipes(rd->replobj);
        replicate_packet_passed(rd->replobj, pi);
    }

    // extract the packet from our iterator
    struct Packet *iterpacket = pi->packet;
    pi->packet = NULL;

    while (list) {
        // do not replicate to masked pipes (member streams)
        if (list->pipe->mask) {
            if (list->next == NULL)
                delete_packet(iterpacket);
            list = list->next;
            continue;
        }
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

static void action_REPL_del(void *action_private)
{
    struct ReplData *rd = (struct ReplData *)action_private;
    struct PipelineList *list = rd->pipes;
    while (list) {
        struct PipelineList *del = list;
        list = list->next;
        pipeline_unref(del->pipe);
        free(del);
    }
    if (rd->replobj)
        pipeline_object_unref(rd->replobj);
}

void create_action_repl(struct Action *a, struct PipelineList *list, struct PipelineObject *replobj, const char *text)
{
    INIT_ACTION(REPL);
    a->del = action_REPL_del;

    struct ReplData *rd = calloc_struct(ReplData);
    rd->pipes = list;
    rd->replobj = replobj;
    if (replobj) {
        pipeline_object_ref(replobj);
        store_replication_pipelines(replobj, list);
    }
    a->action_private = rd;
}

struct PipelineList *action_repl_get_piplinelist(struct Action *a)
{
    if (a->type == ACT_REPL) {
        struct ReplData *rd = (struct ReplData *)a->action_private;
        return rd->pipes;
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////

struct SendData {
    struct Interface *iface;
};

static enum ActionResult action_SEND_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct SendData *sd = (struct SendData *)a->action_private;
    sd->iface->send(sd->iface, pi->packet);
    return ACR_CONTINUE;
}

void create_action_send(struct Action *a, struct Interface *iface, const char *text)
{
    INIT_ACTION(SEND);

    struct SendData *sd = calloc_struct(SendData);
    sd->iface = iface;
    a->action_private = sd;
}

struct Interface *action_send_get_iface(struct Action *a)
{
    if (a->type == ACT_SEND) {
        struct SendData *sd = (struct SendData *)a->action_private;
        return sd->iface;
    } else {
        return NULL;
    }
}

/////////////////////////////////////////////////////////////////////

struct SeqgenData {
    struct PipelineObject *gen;
};

static enum ActionResult action_SEQGEN_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct SeqgenData *sd = (struct SeqgenData *)a->action_private;
    return seq_generator(sd->gen, pi);
}

static void action_SEQGEN_del(void *action_private)
{
    struct SeqgenData *sd = (struct SeqgenData *)action_private;
    pipeline_object_unref(sd->gen);
}

void create_action_seqgen(struct Action *a, struct PipelineObject *gen, const char *text)
{
    INIT_ACTION(SEQGEN);
    a->del = action_SEQGEN_del;

    struct SeqgenData *sd = calloc_struct(SeqgenData);
    sd->gen = gen;
    pipeline_object_ref(gen);
    a->action_private = sd;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_TTLCHECK_execute(struct Action *a, struct PipelineIterator *pi)
{
    (void)a;
    struct Packet *p = pi->packet;

    return p->ttl == 0 ? ACR_DONE : ACR_CONTINUE;
}

void create_action_ttlcheck(struct Action *a, const char *text)
{
    INIT_ACTION(TTLCHECK);
}

/////////////////////////////////////////////////////////////////////

struct TtlData {
    struct HeaderField field;
};

static enum ActionResult action_TTLREDUCE_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct TtlData *td = (struct TtlData *)a->action_private;
    struct Packet *p = pi->packet;
    // we know that in all protocols TTL is 8 bits, byte-aligned
    uint8_t *ttl = p->buf + p->headers[td->field.header_idx].start + td->field.bitoffset/8;

    if (*ttl > 0) *ttl -= 1;
    p->ttl = *ttl;

    return ACR_CONTINUE;
}

void create_action_ttlreduce(struct Action *a, const struct HeaderField *ttlfield, const char *text)
{
    INIT_ACTION(TTLREDUCE);

    struct TtlData *td = calloc_struct(TtlData);
    td->field = *ttlfield;
    a->action_private = td;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_WRITESEQ_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = (struct MetaData *)a->action_private;
    struct Packet *p = pi->packet;

    uint8_t *src = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    uint32_t seq;
    memcpy(&seq, src, len);

    if(p->headers[md->field.header_idx].type == PROTO_ID_IPv6) {
        // this is an SRv6 sequence number, which is only 28 bit
        src = (uint8_t *)&p->sequence;
        uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + (md->field.bitoffset>>3);
        dst[0] |= src[0] & 0x0f;  // write indcator bits, keep the first 4 bits
        dst[1] = src[1];         // write reserved 1 byte
        dst[2] = src[2];         // write seqnum 2 bytes
        dst[3] = src[3];

    // skip if seq already contains OAM associated channel header
    } else if (!SEQ_IS_OAM(seq)) {
        uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
        memcpy(dst, &p->sequence, len);
    }

    return ACR_CONTINUE;
}

void create_action_writeseq(struct Action *a, const struct HeaderField *seqfield, const char *text)
{
    INIT_ACTION(WRITESEQ);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *seqfield;
    a->action_private = md;
}

static enum ActionResult action_WRITETSTAMP_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct MetaData *md = (struct MetaData *)a->action_private;
    struct Packet *p = pi->packet;
    uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    memcpy(dst, &p->timestamp, len);
    return ACR_CONTINUE;
}

void create_action_writetstamp(struct Action *a, const struct HeaderField *tsfield, const char *text)
{
    INIT_ACTION(WRITETSTAMP);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *tsfield;
    a->action_private = md;
}

/////////////////////////////////////////////////////////////////////

struct MepData {
    struct OamEndPoint *oam;
};

static enum ActionResult action_MEP_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct Packet *p = pi->packet;
    struct MepData *md = (struct MepData *)a->action_private;
    // note: we made sure in conf_actions.c that at this point of the pipeline the packet starts with mpls+dcw
    unsigned char *oam_hdr = p->buf + p->headers[1].start;

    if(oam_hdr[0] == 0x11) {
        oam_recv_request(md->oam, pi);
        return ACR_HOLD;
    } else {
        return ACR_CONTINUE;
    }
}

static void action_MEP_del(void *action_private)
{
    struct MepData *md = (struct MepData *)action_private;
    oam_delete_endpoint(md->oam);
}

#define action_MEPSTOP_execute action_MEP_execute
#define action_MIP_execute     action_MEP_execute

void create_action_mepstop(struct Action *a, const char *stream, int level,
        const char *name, const char *text)
{
    INIT_ACTION(MEPSTOP);
    a->del = action_MEP_del;

    struct MepData *md = calloc_struct(MepData);
    md->oam = oam_create_endpoint(name, stream, level, true);
    a->action_private = md;
}

void create_action_mip(struct Action *a, const char *stream, int level,
        const char *name, const char *text)
{
    INIT_ACTION(MIP);
    a->del = action_MEP_del;

    struct MepData *md = calloc_struct(MepData);
    md->oam = oam_create_endpoint(name, stream, level, false);
    a->action_private = md;
}

/////////////////////////////////////////////////////////////////////

struct Action *delete_action(struct Action *a)
{
    if (!a) return NULL;
    if (a->del)
        a->del(a->action_private);
    free(a->action_private);
    free(a->text);
    //free(a); actions are in an array
    return NULL;
}
