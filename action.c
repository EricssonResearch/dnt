// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "action.h"
#include "delay.h"
#include "inet_utils.h"
#include "oam.h"
#include "replicate.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"
#include "pof.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h> /* htonl() */
#include <linux/if_ether.h> /* ETH_P_* */

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
        case ACT_OAMINJECT:
            return "OAMInject";
        case ACT_OAMRECEIVE:
            return "OAMReceive";
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

#define ANALYZE_PROTOSTACK(_data)                                               \
    if (protostack[0] == PROTO_ID_ETH) {                                        \
        for (unsigned i=1; protostack[i]; i++) {                                \
            if (protostack[i] == PROTO_ID_RTAG) {                               \
                _data->rtag_index = i;                                          \
                break;                                                          \
            }                                                                   \
        }                                                                       \
        if (_data->rtag_index == 0) {                                           \
            for (unsigned i=1; protostack[i]; i++) {                            \
                if (protostack[i] == PROTO_ID_PAYLOAD) {                        \
                    _data->last_index = i-1;                                    \
                    const struct ProtocolField *ethertype =                     \
                    protocol_get_field_by_type(protostack[i-1], FT_NEXTHEADER); \
                    _data->ethertype_offset = ethertype->bitoffset / 8;         \
                    break;                                                      \
                }                                                               \
            }                                                                   \
        }                                                                       \
    }


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

    delay_actions++;
}

/////////////////////////////////////////////////////////////////////

static enum ActionResult action_DROP_execute(struct Action *a, struct PipelineIterator *pi)
{
    (void)a;
    (void)pi;
    //struct Packet *p = pi->packet;
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
    const enum ProtocolID *protostack;

    // these are for TSN OAM detection
    unsigned rtag_index;
    unsigned last_index;
    unsigned ethertype_offset;
};

static enum ActionResult action_ELIM_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct ElimData *ed = (struct ElimData *)a->action_private;
    struct Packet *p = pi->packet;

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return ACR_CONTINUE; //TODO ACR_DONE ?
    }

    if (ed->protostack) {
        if (ed->protostack[0] == PROTO_ID_ETH) {
            // we have TSN
            if (ed->rtag_index) {
                unsigned char *rtag_hdr = p->buf + p->headers[ed->rtag_index].start;
                bool packet_is_oam = (rtag_hdr[0] & 0xf0) == 0x10;

                if (packet_is_oam) {
                    unsigned char *p_smac = p->buf + p->headers[0].start + 6;
                    unsigned char *p_seq = p->buf + p->headers[ed->rtag_index].start + 1;
                    unsigned char *p_sessionid = p->buf + p->headers[ed->rtag_index].start + 3;
                    unsigned char *p_level = p->buf + p->headers[ed->rtag_index+1].start;
                    char smac_str[ETHER_ADDSTRLEN];
                    ether_ntop(p_smac, smac_str, ETHER_ADDSTRLEN);
                    unsigned char sessionid = (*p_sessionid) & 0x0f;
                    unsigned char level = (*p_level) >> 5;
                    char *session = strdup_printf("%s:%hhu:%hhu", smac_str, sessionid, level);
                    log_debug("TSN session %s", session);

                    enum ActionResult ret = oam_recovery(ed->rcvy, pi->packet, session, *p_seq);
                    free(session);
                    return ret;
                }
            } else {
                // TODO if we don't have RTAG then how do we eliminate the data packets??
                //  -> maybe there was one when we did READSEQ, but it has been deleted
                //  TODO what can we do here?
            }
        } else if (ed->protostack[0] == PROTO_ID_MPLS) {
            // we have DetNet PseudoWire
            if (SEQ_IS_OAM(p->sequence)) { //TODO what if READSEQ used a different header?
                                           // TODO support >1 mpls label
                INTERPRET_DACH(p->buf + p->headers[1].start);
                char nodeid_str[10];
                snprintf(nodeid_str, sizeof(nodeid_str), "%u", dach.nodeid);
                char *session = strdup_printf("%s:%hhu:%hhu", nodeid_str, dach.session, dach.level);
                log_debug("PW session %s", session);

                enum ActionResult ret = oam_recovery(ed->rcvy, pi->packet, session, dach.seq);
                free(session);
                return ret;
            }
        } else {
            //TODO die?
        }
    }

    // the sequence number for recovery is pi->packet->sequence
    return ed->rcvy->process_packet(ed->rcvy, pi);
}

static void action_ELIM_del(void *action_private)
{
    struct ElimData *ed = (struct ElimData *)action_private;
    pipeline_object_unref(ed->rcvy);
}

void create_action_elim(struct Action *a, struct PipelineObject *rcvy, const enum ProtocolID *protostack, const char *text)
{
    INIT_ACTION(ELIM);
    a->del = action_ELIM_del;

    struct ElimData *ed = calloc_struct(ElimData);
    ed->rcvy = rcvy;
    ed->protostack = protostack;
    if (protostack) {
        ANALYZE_PROTOSTACK(ed);
    }
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

    //TODO we have to handle both TSN and DetNet packets
    //  do we have to change something here?
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

struct OamInjectData {
    struct OAM_MaintenancePoint *mp;
};

static void action_OAMINJECT_del(void *action_private)
{
    struct OamInjectData *oid = (struct OamInjectData *)action_private;
    oam_unref_maintenance_point(oid->mp);
}

static enum ActionResult action_OAMINJECT_execute(struct Action *a, struct PipelineIterator *pi)
{
    (void)a;
    (void)pi;
    return ACR_CONTINUE;
}

bool create_action_oam_inject(struct Action *a, const char *name, const char *stream, int level,
                              bool intermediate, struct Pipeline *pipe, unsigned idx,
                              struct OAM_MP_Address *address,
                              const enum ProtocolID *protostack,
                              struct PipelineObject *obj, const char *text)
{
    INIT_ACTION(OAMINJECT);
    a->del = action_OAMINJECT_del;

    enum OAM_MP_Type type = intermediate ? OAM_Intermediate : OAM_Start;
    struct OAM_MaintenancePoint *mp = oam_new_maintenance_point(stream, name, type, level, protostack, obj, pipe, idx, address);
    if (mp == NULL) {
        log_error("failed to create maintenance point for inject action %s", name);
        return false;
    }
    struct OamInjectData *oid = calloc_struct(OamInjectData);
    oid->mp = mp;
    a->action_private = oid;
    return true;
}

/////////////////////////////////////////////////////////////////////

struct OamReceiveData {
    struct OAM_MaintenancePoint *mp;
    const enum ProtocolID *protostack;

    // these are for TSN OAM detection
    unsigned rtag_index;
    unsigned last_index;
    unsigned ethertype_offset;
};

static void action_OAMRECEIVE_del(void *action_private)
{
    struct OamReceiveData *ord = (struct OamReceiveData *)action_private;
    oam_unref_maintenance_point(ord->mp);
}

static enum ActionResult action_OAMRECEIVE_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct OamReceiveData *ord = (struct OamReceiveData *)a->action_private;
    struct Packet *p = pi->packet;

    bool packet_is_oam = false;
    if (ord->protostack[0] == PROTO_ID_ETH) {
        // we have TSN
        if (ord->rtag_index) {
            unsigned char *rtag_hdr = p->buf + p->headers[ord->rtag_index].start;
            packet_is_oam = (rtag_hdr[0] & 0xf0) == 0x10;
        } else {
            unsigned char *ethertype = p->buf + p->headers[ord->last_index].start + ord->ethertype_offset;
            packet_is_oam = (ethertype[0] == ((ETH_P_CFM >> 8) & 0xff))
                            && (ethertype[1] == (ETH_P_CFM & 0xff));
        }
    } else if (ord->protostack[0] == PROTO_ID_MPLS) {
        // we have DetNet PseudoWire
        unsigned char *oam_hdr = p->buf + p->headers[1].start;
        packet_is_oam = (oam_hdr[0] & 0xf0) == 0x10;
    } else {
        //TODO die?
        return ACR_CONTINUE;
    }

    if (packet_is_oam) {
        oam_receive_inband(ord->mp, pi);
        return ACR_HOLD;
    } else {
        return ACR_CONTINUE;
    }
}

bool create_action_oam_receive(struct Action *a, const char *name, const char *stream, int level,
                               bool intermediate, const enum ProtocolID *protostack,
                               struct PipelineObject *obj, const char *text)
{
    INIT_ACTION(OAMRECEIVE);
    a->del = action_OAMRECEIVE_del;

    enum OAM_MP_Type type = intermediate ? OAM_Intermediate : OAM_Stop;
    struct OAM_MaintenancePoint *mp = oam_new_maintenance_point(stream, name, type, level, protostack, obj, NULL, 0, NULL);
    if (mp == NULL) {
        log_error("failed to create maintenance point for receive action %s", name);
        return false;
    }
    struct OamReceiveData *ord = calloc_struct(OamReceiveData);
    ord->mp = mp;
    ord->protostack = protostack;
    ANALYZE_PROTOSTACK(ord);
    a->action_private = ord;
    return true;

}

/////////////////////////////////////////////////////////////////////

struct PofData {
    struct PipelineObject *pof;
};

static enum ActionResult action_POF_execute(struct Action *a, struct PipelineIterator *pi)
{
    struct PofData *pd = (struct PofData *)a->action_private;
    return pd->pof->process_packet(pd->pof, pi);
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
        rd->replobj->process_packet(rd->replobj, pi);
    }

    // extract the packet from our iterator
    // TODO pipe_iterator_extract_packet()
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
    return sd->gen->process_packet(sd->gen, pi);
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

    if (p->headers[md->field.header_idx].type == PROTO_ID_IPv6) {
        // this is an SRv6 sequence number, which is only 28 bit
        uint8_t *src = (uint8_t *)&p->sequence;
        uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + (md->field.bitoffset>>3);
        dst[0] = (dst[0] & 0xf0) + (src[0] & 0x0f);  // write indcator bits, keep the first 4 bits
        dst[1] = src[1];         // write reserved 1 byte
        dst[2] = src[2];         // write seqnum 2 bytes
        dst[3] = src[3];

    } else {
        uint8_t *dst = p->buf + p->headers[md->field.header_idx].start + md->field.bitoffset/8;
        memcpy(dst, &p->sequence, 4);
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
