// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "action.h"
#include "conf_object.h"
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
#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h> /* htonl() */

#define OAM_INDICATOR_MASK 0x10000000u

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
    struct AddData *ad = a->action_private;
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
    struct DelData *dd = a->action_private;
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
    unsigned delay_ms;
    //struct HeaderField timestamp;
};

static enum ActionResult action_DELAY_execute(struct Action *a, struct PipelineIterator *pi)
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

void create_action_delay(struct Action *a, unsigned delay_ms, const char *text)
{
    INIT_ACTION(DELAY);

    struct DelayData *dd = calloc_struct(DelayData);
    dd->delay_ms = delay_ms;
    a->action_private = dd;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_DROP_execute(struct Action *a, struct PipelineIterator *pi)
{
    (void)a;
    (void)pi;
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
}

void create_action_edit(struct Action *a, struct EditAssign *assigns, unsigned assign_count, const char *text)
{
    INIT_ACTION(EDIT);
    a->del = action_edit_del;

    struct EditData *ed = calloc_struct(EditData);
    ed->assigns = assigns;
    ed->assign_count = assign_count;
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

struct ElimData {
    struct SequenceRecovery *rcvy;
};

static char *get_oam_key(const struct Packet *p)
{
    // TODO: we can assume headers[1] indentified?
    const uint8_t *g_ach = p->buf + p->headers[1].start;
    char key[4] = { };
    key[0] = g_ach[4]; //node ID MSB
    key[1] = g_ach[5]; //node ID LSB
    key[2] = g_ach[7] & 0x0f;
    key[3] = 0;
    return strdup(key);
}

static enum ActionResult action_ELIM_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ElimData *ed = a->action_private;
    const struct Packet *p = pi->packet;
    uint32_t seq = ntohl(p->sequence);
    if ((seq & OAM_INDICATOR_MASK) == 0) {
        if (seq_recovery(ed->rcvy, seq)) {
            return ACR_CONTINUE;
        } else {
            return ACR_DONE;
        }
    } else {
        // This is an OAM packet, so we read the node ID and session ID
        // and create/get the temporary SeqRcvy instance using these as key
        struct SequenceRecovery *oam_rec = get_oam_rcvy(get_oam_key(pi->packet));
        const uint8_t oam_seq = (seq >> 16) & 0xff;
        if (seq_recovery(oam_rec, oam_seq)) {
            return ACR_CONTINUE;
        } else {
            return ACR_DONE;
        }
    }
}

void create_action_elim(struct Action *a, struct SequenceRecovery *rcvy, const char *text)
{
    INIT_ACTION(ELIM);

    struct ElimData *ed = calloc_struct(ElimData);
    ed->rcvy = rcvy;
    a->action_private = ed;
}

/////////////////////////////////////////////////////////////////////

struct FilterOamData {
    struct HeaderField field;
};

static enum ActionResult action_FILTEROAM_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct FilterOamData *fd = a->action_private;

    struct Packet *p = pi->packet;
    uint8_t *src = p->buf + p->headers[fd->field.header_idx].start + fd->field.bitoffset/8;
    unsigned len = fd->field.bitcount/8; //TODO this is always 4
    uint32_t seq;
    memcpy(&seq, src, len);

    if (ntohl(seq) & OAM_INDICATOR_MASK) {
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
    struct Pof *pof;
};

static enum ActionResult action_POF_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct PofData *pd = a->action_private;
    if (pof_insert(pd->pof, pi)) {
        return ACR_HOLD;
    } else {
        return ACR_DONE;
    }
}

void create_action_pof(struct Action *a, struct Pof *pof, const char *text)
{
    INIT_ACTION(POF);

    struct PofData *pd = calloc_struct(PofData);
    pd->pof = pof;
    a->action_private = pd;
}

/////////////////////////////////////////////////////////////////////

struct MetaData {
    struct HeaderField field;
};

static enum ActionResult action_READSEQ_execute(struct Action *a, struct PipelineIterator *pi)
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
    INIT_ACTION(READSEQ);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *seqfield;
    a->action_private = md;
}

static enum ActionResult action_READTSTAMP_execute(struct Action *a, struct PipelineIterator *pi)
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
    INIT_ACTION(READTSTAMP);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *tsfield;
    a->action_private = md;
}

/////////////////////////////////////////////////////////////////////

struct ReplData {
    struct PipelineList *pipes;
    struct Replicate *replobj;
};

static enum ActionResult action_REPL_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ReplData *rd = a->action_private;

    // extract the packet from our iterator
    struct Packet *iterpacket = pi->packet;
    pi->packet = NULL;

    struct PipelineList *list = rd->pipes;
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
    if (rd->replobj)
        replicate_packet_passed(rd->replobj);
    return ACR_DONE;
}

static void action_repl_del(void *action_private)
{
    struct ReplData *rd = action_private;
    struct PipelineList *list = rd->pipes;
    while (list) {
        struct PipelineList *del = list;
        list = list->next;
        pipeline_unref(del->pipe);
        free(del);
    }
}

void create_action_repl(struct Action *a, struct PipelineList *list, struct Replicate *replobj, const char *text)
{
    INIT_ACTION(REPL);
    a->del = action_repl_del;

    struct ReplData *rd = calloc_struct(ReplData);
    rd->pipes = list;
    rd->replobj = replobj;
    a->action_private = rd;
}

struct PipelineList *action_repl_get_piplinelist(struct Action *a)
{
    if (a->type == ACT_REPL) {
        struct ReplData *rd = a->action_private;
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
    struct SendData *sd = a->action_private;
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

static enum ActionResult action_SEQGEN_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct SeqgenData *sd = a->action_private;
    seq_generator(sd->gen, pi->packet);
    return ACR_CONTINUE;
}

void create_action_seqgen(struct Action *a, struct SequenceGenerator *gen, const char *text)
{
    INIT_ACTION(SEQGEN);

    struct SeqgenData *sd = calloc_struct(SeqgenData);
    sd->gen = gen;
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
    struct TtlData *td = a->action_private;
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
    struct MetaData *md = a->action_private;
    struct Packet *p = pi->packet;

    uint8_t *src = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
    unsigned len = md->field.bitcount/8; //TODO this is always 4
    uint32_t seq;
    memcpy(&seq, src, len);

    // skip if seq already contains OAM associated channel header
    if ((ntohl(seq) & OAM_INDICATOR_MASK) == 0) {
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
    INIT_ACTION(WRITETSTAMP);

    struct MetaData *md = calloc_struct(MetaData);
    md->field = *tsfield;
    a->action_private = md;
}

/////////////////////////////////////////////////////////////////////

struct OamData {
    struct ConfObject *target; // PRF, PEF, POF, etc.
    char *name;
    int level;
};

static enum ActionResult action_MEPSTOP_execute(struct Action *a, struct PipelineIterator *pi)
{
    //TODO: similarly to MIP
    (void) a;
    (void) pi;
    return ACR_CONTINUE;
}

void create_action_mepstop(struct Action *a, int level, struct ConfObject *target, const char *name, const char *text)
{
    INIT_ACTION(MEPSTOP);

    struct OamData *oam = calloc_struct(OamData);
    oam->target = target;
    oam->name = strdup(name);
    oam->level = level;

    a->action_private = oam;
}

static enum ActionResult action_MIP_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct Packet *p = pi->packet;
    struct OamData *oam  = a->action_private;
    unsigned char *oam_hdr = p->buf + p->headers[1].start;

    if(oam_hdr[0] == 0x11){
        unsigned char seq = oam_hdr[1];
        unsigned short channel = (oam_hdr[2]<<8)+oam_hdr[3];
        unsigned short nodeid = (oam_hdr[4]<<8)+oam_hdr[5];
        unsigned char level = oam_hdr[6] >> 1;
        unsigned char session = oam_hdr[7] & 0x0f;
        char *msg;
        if(p->headers[1].type == PROTO_ID_DCW)
            msg = (char *)(p->buf + p->headers[2].start + 4);
        else // OAM packet
            msg = (char *)(p->buf + p->headers[2].start);
        int port=6634;
        char *reply_address=NULL;

        printf("OAM packet (%s) at MIP [%s level %d], ttl %d nib_ver %x sequence %x channel %x node %x level %x session %x\njson: %s\n",
                protocol_type_from_id(p->headers[1].type), oam->name, oam->level, p->ttl, oam_hdr[0], seq, channel, nodeid, level, session, msg);

        struct JsonValue *j = json_parse(msg, strlen(msg));
        if(j==NULL){
            perror("No json string in incoming OAM packet");
            return ACR_DONE;
        }

        // if record route, add this hop
        struct JsonValue *jrr = hashmap_find(j->v.object, "rr");
        if(jrr!=NULL){
            //char hop[32];
            //sprintf(hop, "%d", hashmap_count(jrr->v.object));
            json_array_unshift(jrr, json_string(oam->name));
            //json_object_insert(jrr, hop, json_string(oam->name));
            /* packet_del_header(p, 2); */

            unsigned js_length;
            char *js_string = json_serialize(j, &js_length);
            if (js_string == NULL) {
                perror("action_MIP_execute: json string empty");
                return ACR_DONE;            //  DROP packet
            }
            /* packet_add_header(p, 2, PROTO_ID_PAYLOAD, js_length); */
            /* msg = (char *)(p->buf + p->headers[2].start); */
            memcpy(msg, js_string, js_length);
            if(p->headers[1].type == PROTO_ID_DCW) js_length += 4;
            p->len = p->len - p->headers[2].len + js_length;
            p->headers[2].len = js_length;
            p->header_count = 3;
            free(js_string);
        }

        // MIP message handling logic
        if(level < oam->level){
            fprintf(stderr, "MIP %s level %d Warning: dropping lower level (level %d) OAM packet.\n", oam->name, oam->level, level);
            return ACR_DONE;            // if lower level, DROP packet
        }
        if(level > oam->level)
            return ACR_CONTINUE;        // if lower level, forward packet

        // get target from json
        struct JsonValue *target = hashmap_find(j->v.object, "target");

        // continue and send response if ttl=0 or target is us or target is "any"
        if( (p->ttl != 0) && (strcmp(target->v.string, oam->name)!=0) && (strcmp(target->v.string, "any")!=0)){
            json_delete(j);
            return ACR_CONTINUE;
        }

        // send reply
        struct JsonValue *jret = hashmap_find(j->v.object, "return");
        if(jret==NULL)
            perror("Not found json object 'return'");
        struct JsonValue *val = hashmap_find(jret->v.object, "port");
        if(val!=NULL)
            port=val->v.number;
        val = hashmap_find(jret->v.object, "ip");
        if(val!=NULL)
            reply_address = strdup(val->v.string);
        else
            return ACR_CONTINUE;

        // if object state is requested
        struct JsonValue *jos = hashmap_find(j->v.object, "objects");
        if(jos!=NULL){
            struct JsonValue *objinfo = NULL;
            if (oam->target && oam->target->print_state) {
                objinfo = oam->target->print_state(oam->target->object);
                json_object_insert(j, "objects", objinfo);
            }
        }

        hashmap_remove(j->v.object,"return");
        json_object_insert(j, "sequence", json_number(seq));
        json_object_insert(j, "nodeid", json_number(nodeid));
        json_object_insert(j, "node", json_string(oam->name));
        json_object_insert(j, "session", json_number(session));
        unsigned msg_len=0;
        char *j_msg = json_serialize(j, &msg_len);
        //printf("Send to %s : %d\nlen %d %s\n", reply_address, port, msg_len, j_msg);
        oam_send_reply(reply_address, port, j_msg, msg_len);
        json_delete(j);
        free(reply_address);
        free(j_msg);

        if( (p->ttl == 0) || (strcmp(target->v.string, oam->name)==0) )
            return ACR_DONE;            // drop if ttl 0 or we were the target

    }
    return ACR_CONTINUE;
}

void create_action_mip(struct Action *a, int level, struct ConfObject *target, const char *name, const char *text)
{
    INIT_ACTION(MIP);

    struct OamData *oam = calloc_struct(OamData);
    oam->target = target;
    oam->name = strdup(name);
    oam->level = level;

    a->action_private = oam;
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
