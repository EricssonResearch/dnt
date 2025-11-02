// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_maintenance.h"
#include "oam_request.h"
#include "oam_core.h"

#include "hashmap.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "seq_recov.h"
#include "thread_utils.h"
#include "utils.h"
#include "if_oam.h"
#include "interface.h"

#include <stdlib.h>
#include <arpa/inet.h>        // inet_pton()
#include <netinet/icmp6.h>    // ICMP6_ECHO_REQUEST

DEFAULT_LOGGING_MODULE(OAM, INFO);

#define OAM_CHANNEL 0x7fff /* Experimental channel type, we are not compatible with anything */

#define ICMP6_PRIVATE_EXP 200 /* Private experimentation, as defined in RFC 4443  */

struct MP_SRv6_Address {
    unsigned char loc[8];
    enum OAM_MP_Addr_Source loc_source;
};

struct MP_MPLS_Address {
    unsigned char label[4];
    enum OAM_MP_Addr_Source label_source;
};

struct MP_TSN_Address {
    unsigned char dmac[6];
    unsigned char vlan[2];
    enum OAM_MP_Addr_Source dmac_source;
    enum OAM_MP_Addr_Source vlan_source;
};

struct OAM_MaintenancePoint {
    char *name;
    char *stream_name;
    enum OAM_MP_Type type;
    enum OAM_MP_Encap encap;
    unsigned level;

    int reference_count;

    struct Pipeline *pipe;
    int pipe_pos_idx;

    const enum ProtocolID *protostack;
    union {
        struct MP_SRv6_Address sid_address;
        struct MP_MPLS_Address pw_address;
        struct MP_TSN_Address tsn_address;
    };
    bool address_ok;

    struct PipelineObject *object;

    unsigned oam_send;
    unsigned oam_recv;

    struct MessageQueue *mask_queue;
    struct Thread *mask_send; // periodic sending of mask signal
    struct Thread *mask_recv; // timeout for receiving mask signal
};

// note: this hash doesn't hold reference to the MPs in it
static struct HashMap *mp_hash = NULL; // name -> struct OAM_MaintenancePoint


static int mp_delete_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)value;

    notification_register_source(key, NULL, NULL, 0);
    if (mp->object)
        pipeline_object_unref(mp->object);

    if (mp->mask_queue) {
        log_info("mp_delete_cb %s with mask_queue", key);
        if (mp->mask_send) {
            messagequeue_push(mp->mask_queue, mp);
            thread_join(mp->mask_send);
        }
        if (mp->mask_recv) {
            thread_stop(mp->mask_recv);
        }
        delete_messagequeue(mp->mask_queue);
    }
    free(mp->name);
    free(mp->stream_name);
    free(mp);

    return 1;
}

static NotificationLevel mp_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)self;
    struct JsonValue *state = mp_get_state_json(mp, true);
    *msg = state;
    return NOTIF_PULL;
}

/* Internet checksum (RFC 1071) */
static uint32_t checksum16_partial(const void * buf, size_t len, uint32_t sum) {
    const uint16_t *data = (const uint16_t* ) buf;

    while (len > 1) {
        sum += *data++;
        len -= 2;
    }

    if (len) {  // odd byte
        sum += *((const uint8_t *)data);
    }

    return sum;
}

/* Compute ICMPv6 checksum without copying */
static uint16_t icmp6_checksum(unsigned char *src,
                        unsigned char *dst,
                        const void *icmp_pkt,
                        size_t icmp_len) {
    uint32_t sum = 0;

    // Source and destination addresses
    sum = checksum16_partial(src, 16, sum);
    sum = checksum16_partial(dst, 16, sum);

    // Upper-layer packet length (32-bit)
    uint8_t l[4];
    l[0] = (icmp_len >> 24) & 0xFF;
    l[1] = (icmp_len >> 16) & 0xFF;
    l[2] = (icmp_len >> 8)  & 0xFF;
    l[3] = (icmp_len)       & 0xFF;
    sum = checksum16_partial(l, sizeof(l), sum);

    // 3 bytes zero + next header
    uint8_t nh_field[4] = {0,0,0,IPPROTO_ICMPV6};
    sum = checksum16_partial(nh_field, sizeof(nh_field), sum);

    // ICMPv6 header + payload
    sum = checksum16_partial(icmp_pkt, icmp_len, sum);

    // finalize checksum
    sum = (sum>>16)+(sum & 0xffff);
    sum = sum + (sum>>16);
    return (uint16_t)(~sum);
}


static struct JsonValue *unpack_srv6_message(const struct Packet *p)
{
    if (p->header_count < 3) {
        log_error("OAM packet doesn't have 2 identified headers (ipv6, ipv6/ipv4/tsn, payload), how was this matched??");
        return NULL;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return NULL;
    }

    // OAM nibble is already checked here, so check next headers (ipv6, icmpv6)
    unsigned char *ipv6_hdr  = p->buf + p->headers[0].start;
    if(ipv6_hdr[6] != IPPROTO_IPV6) {
        log_error("Not IPv6 OAM packet, can not unpack");
        return NULL;
    }
    ipv6_hdr  = p->buf + p->headers[1].start;
    if(ipv6_hdr[6] != IPPROTO_ICMPV6) {
        log_error("Not ICMPv6 OAM packet, can not unpack");
        return NULL;
    }

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_IPv6)->bytelength +
                          protocol_from_id(PROTO_ID_IPv6)->bytelength +
                          protocol_from_id(PROTO_ID_ICMPv6)->bytelength;

    if (plen < header_len) {
        log_error("SRv6 OAM packet is too short");
        return NULL;
    }

    unsigned char *icmp6 = p->buf + p->headers[2].start;

    // json is after the ICMPv6 header in payload
    char *json_str = (char*)(p->buf + p->headers[2].start + protocol_from_id(PROTO_ID_ICMPv6)->bytelength);
    unsigned json_len = plen - header_len;

    char *jerror;
    struct JsonValue *js = json_parse(json_str, json_len, &jerror);
    if (js == NULL || js->type != JSON_OBJECT) {
        log_error("Received SRv6 OAM packet contains invalid JSON string: %s", jerror);
        free(jerror);
        return NULL;
    }

    json_object_insert(js, "session", json_number(icmp6[5] & 0x0f));
    json_object_insert(js, "seq", json_number(icmp6[7]));
    json_object_insert(js, "level", json_number((icmp6[5] >> 4) & 0x07));
    json_object_insert(js, "nodeid", json_number((ipv6_hdr[23] << 8) | ipv6_hdr[22]));  // last 2 bytes of IPv6 source addr.

    return js;
}

static struct JsonValue *pack_srv6_message_header(const struct OAM_MaintenancePoint *mp, struct Packet *p,
        const struct OamRequest *req)
{
    unsigned nodeid;
    unsigned char level;
    unsigned char session;
    unsigned char seq;
    unsigned char ttl;

    request_get_identification_data(req, &nodeid, &level, &session, &seq, &ttl);

    packet_clear_headers(p);
    packet_enlarge_scratch(p);
    packet_add_header(p, 0, PROTO_ID_IPv6, protocol_from_id(PROTO_ID_IPv6)->bytelength);
    packet_add_header(p, 1, PROTO_ID_IPv6, protocol_from_id(PROTO_ID_IPv6)->bytelength);
    packet_add_header(p, 2, PROTO_ID_ICMPv6, protocol_from_id(PROTO_ID_ICMPv6)->bytelength);
    packet_add_header(p, 3, PROTO_ID_PAYLOAD, 0);

    unsigned char *ipv6_detnetsid  = p->buf + p->headers[0].start;
    ipv6_detnetsid[0]=0x60;
    ipv6_detnetsid[1]=0; ipv6_detnetsid[2]=0; ipv6_detnetsid[3]=0;  // flow label
    //ipv6_detnetsid[4-5] is length
    ipv6_detnetsid[6] = IPPROTO_IPV6; // next header
    ipv6_detnetsid[7] = ttl;  // hop count
    memset(&ipv6_detnetsid[8], 0, 16);  // if saddr is zero, the interface send will fill
    memcpy(&ipv6_detnetsid[24], mp->sid_address.loc, 16);
    ipv6_detnetsid[39] = seq;   // fill in seqnum in SID

    // Indicate OAM by setting the `o` bit in sequence number field
    ipv6_detnetsid[36] |= 0x01;

    unsigned char *ipv6 = p->buf + p->headers[1].start;
    ipv6[0]=0x60;
    ipv6[1]=0; ipv6[2]=0; ipv6[3]=0;  // flow label
    //ipv6[4..5] is length
    ipv6[6] = IPPROTO_ICMPV6; // next header
    ipv6[7] = ttl;   // hop count

    struct sockaddr_in6 sa6;
    if(inet_pton(AF_INET6, oamif_get_ip(get_default_oam_ip_interface()), &(sa6.sin6_addr)) <= 0) {
        log_warning_once("OAM interface '%s' does not have IPv6 address.", get_default_oam_ip_interface()->name);
        memset(&ipv6[8], 0, 16);    // source addr ::0 (unknown addr)
    } else
        memcpy(&ipv6[8], &sa6.sin6_addr, 16);
    memset(&ipv6[24], 0, 16); ipv6[39]=1;       // ::1

    unsigned char *icmpv6  = p->buf + p->headers[2].start;
    icmpv6[0] = ICMP6_ECHO_REQUEST;     // type
    //icmpv6[0] = ICMP6_PRIVATE_EXP;        // type 200  Private experimentation
    icmpv6[1] = 0;                        // code
    //icmpv6[2] = 0; icmpv6[3] = 0;       // checksum will be calculated later

    // Type specific ICMPv6 fields
    icmpv6[5] = ((level & 0x07) << 4) | (session & 0x0f);  // Identifier
    icmpv6[4] = 0;              // Network byte order is Big Endian
    icmpv6[7] = seq;            // Sequence
    icmpv6[6] = 0;              // Network byte order is Big Endian
                                // (must be in line with elimination action!)

    p->ttl = ttl;
    p->sequence = htonl(seq);

    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string(request_get_type(req)));
    json_object_insert(js, "code", json_string("request"));

    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    p->recv_time = sendtime;
    timespec_to_tsntstamp(p->timestamp, &sendtime);

    json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
    json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));

    return js;
}

static bool pack_srv6_payload(struct Packet *p, const struct JsonValue *msg)
{
    if (!packet_is_linear(p)) {
        log_error("can't update message in packet that is not continuous in memory");
        return false;
    }

    unsigned js_length;
    char *js_string = json_serialize(msg, &js_length);
    if (js_string == NULL) {
        log_error("could not serialize the updated message");
        return false;
    }

    // here we may have only 3 headers identified if packet is forwareded in-band
    unsigned header_len = protocol_from_id(PROTO_ID_ICMPv6)->bytelength;

    // write the new json string into p
    char *payload = (char *)(p->buf + p->headers[2].start + header_len);
    memcpy(payload, js_string, js_length);
    free(js_string);
    unsigned new_len = header_len + js_length;

    // update packet len in inner header
    unsigned char *ipv6  = p->buf + p->headers[1].start;
    ipv6[4] = (new_len >> 8) & 0xff;
    ipv6[5] = new_len & 0xff;

    // calc ICMPv6 checksum
    unsigned char *icmpv6  = p->buf + p->headers[2].start;
    icmpv6[2] = 0; icmpv6[3] = 0;   // clear old checksum
    uint16_t sum = icmp6_checksum(&ipv6[8], &ipv6[24], icmpv6, new_len);
    icmpv6[3] = (sum >> 8) & 0xff;
    icmpv6[2] = sum & 0xff;

    // add first 2 header lens
    new_len += p->headers[0].len+p->headers[1].len;

    // note: we don't know how many and what type of headers there are in @p
    //       we only know that the packet is linear in memory
    if (p->headers[0].start < p->start) {
        // data is on the scratch
        p->headers[p->header_count-1].len += new_len - p->scratch_len;
        p->scratch_len = new_len;
    } else {
        // data is in the receive area
        p->headers[p->header_count-1].len += new_len - p->len;
        p->len = new_len;
    }
    return true;
}

static int compare_srv6_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("SRv6 OAM packet doesn't have 2 identified headers (ipv6, ipv6), how was this matched??");
        return -1;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return -1;
    }

    // here we either have IP6+IP6+payload or IP6+IP6+ICMPv6+payload
    unsigned header_len = protocol_from_id(PROTO_ID_IPv6)->bytelength;

    unsigned char *p_level_session = p->buf + p->headers[1].start + header_len + 5;     // level|session is in the 5th byte of ICMPv6
    unsigned char level = ((*p_level_session) >> 4) & 0x07;

    return (int)level - (int)mp->level;
}

static unsigned char get_srv6_ttl(const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("SRv6 OAM packet doesn't have 2 identified headers (ipv6, ipv6), how was this matched??");
        return -1;
    }

    // get it from DetNet SID header
    unsigned char *ipv6_start = p->buf + p->headers[0].start;
    return ipv6_start[7];
}

static struct JsonValue *unpack_pw_message(const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return NULL;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return NULL;
    }

    //TODO support >1 mpls headers?
    //  use protostack

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_MPLS)->bytelength +
        protocol_from_id(PROTO_ID_OAM)->bytelength;

    if (plen < header_len) {
        log_error("PW OAM packet is too short");
        return NULL;
    }

    unsigned char *dach_start = p->buf + p->headers[0].start + protocol_from_id(PROTO_ID_MPLS)->bytelength;
    char *json_str = (char*)(dach_start + protocol_from_id(PROTO_ID_OAM)->bytelength);
    unsigned json_len = plen - header_len;

    //TODO decode mpls label into the json?

    INTERPRET_DACH(dach_start);

    char *jerror;
    struct JsonValue *js = json_parse(json_str, json_len, &jerror);
    if (js == NULL || js->type != JSON_OBJECT) {
        log_error("Received PW OAM packet contains invalid JSON string: %s", jerror);
        free(jerror);
        return NULL;
    }

#define ADD_DACH_FIELD(_name)                                               \
    do {                                                                    \
        struct JsonValue *v = json_object_get_any(js, #_name);              \
        if (v) {                                                            \
            log_warning("Received PW OAM packet's JSON contains " #_name);  \
        }                                                                   \
        json_object_insert(js, #_name, json_number(dach._name));            \
    } while (0)

    ADD_DACH_FIELD(version);
    ADD_DACH_FIELD(seq);
    ADD_DACH_FIELD(channel);
    ADD_DACH_FIELD(nodeid);
    ADD_DACH_FIELD(level);
    ADD_DACH_FIELD(flags);
    ADD_DACH_FIELD(session);

#undef ADD_DACH_FIELD

    return js;
}

static struct JsonValue *pack_pw_message_header(const struct OAM_MaintenancePoint *mp, struct Packet *p,
        const struct OamRequest *req)
{
    unsigned channel = OAM_CHANNEL;
    unsigned nodeid;
    unsigned char level;
    unsigned char session;
    unsigned char seq;
    unsigned char ttl;

    request_get_identification_data(req, &nodeid, &level, &session, &seq, &ttl);

    packet_clear_headers(p);
    packet_enlarge_scratch(p);

    // TODO support >1 mpls label?

    packet_add_header(p, 0, PROTO_ID_MPLS, protocol_from_id(PROTO_ID_MPLS)->bytelength);
    packet_add_header(p, 1, PROTO_ID_OAM, protocol_from_id(PROTO_ID_OAM)->bytelength);
    packet_add_header(p, 2, PROTO_ID_PAYLOAD, 0);

    unsigned char *mpls = p->buf + p->headers[0].start;
    memcpy(mpls, mp->pw_address.label, 4);
    mpls[2] |= 1; // BOS
    mpls[3] = ttl;
    unsigned char *oam  = p->buf + p->headers[1].start;
    oam[0] = 0x10; // indicator and version
    oam[1] = seq;
    oam[2] = (channel>>8) & 0xff;
    oam[3] = channel & 0xff;
    oam[4] = (nodeid>>12) & 0xff;
    oam[5] = (nodeid>>4) & 0xff;
    oam[6] = ((nodeid&0xf) << 4) + ((level & 0x07) << 1);
    oam[7] = session & 0x0f;

    // make sure writeseq works fine
    p->sequence = (oam[0]<<0) + (oam[1]<<8) + (oam[2]<<16) + (oam[3]<<24);

    p->ttl = ttl;

    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string(request_get_type(req)));
    json_object_insert(js, "code", json_string("request"));

    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    p->recv_time = sendtime;
    timespec_to_tsntstamp(p->timestamp, &sendtime);

    json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
    json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));

    return js;
}

static bool pack_pw_payload(struct Packet *p, const struct JsonValue *msg)
{
    if (!packet_is_linear(p)) {
        log_error("can't update message in packet that is not continuous in memory");
        return false;
    }

    struct JsonValue *out = json_duplicate(msg);
    const char *ach_keys[] = { "version", "seq", "channel", "nodeid", "level", "flags", "session" };
    for (unsigned i=0; i<ARRAY_SIZE(ach_keys); i++) {
        json_object_remove(out, ach_keys[i]);
    }

    unsigned js_length;
    char *js_string = json_serialize(out, &js_length);
    if (js_string == NULL) {
        log_error("could not serialize the updated message");
        json_delete(out);
        return false;
    }
    json_delete(out);

    // TODO support >1 mpls label?

    // write the new json string into p
    // TODO this packet manipulation is extremely ugly
    unsigned header_len = protocol_from_id(PROTO_ID_MPLS)->bytelength +
        protocol_from_id(PROTO_ID_OAM)->bytelength;
    char *payload = (char *)(p->buf + p->headers[0].start + header_len);
    memcpy(payload, js_string, js_length);
    free(js_string);
    unsigned new_len = header_len + js_length;
    // note: we don't know how many and what type of headers there are in @p
    //       we only know that the packet is linear in memory
    if (p->headers[0].start < p->start) {
        // data is on the scratch
        p->headers[p->header_count-1].len += new_len - p->scratch_len;
        p->scratch_len = new_len;
    } else {
        // data is in the receive area
        p->headers[p->header_count-1].len += new_len - p->len;
        p->len = new_len;
    }
    return true;
}

static int compare_pw_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return -1;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return -1;
    }

    //TODO support >1 mpls headers?
    //  use protostack

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_MPLS)->bytelength +
        protocol_from_id(PROTO_ID_OAM)->bytelength;

    if (plen < header_len) {
        log_error("PW OAM packet is too short");
        return -1;
    }

    unsigned char *dach_start = p->buf + p->headers[0].start + protocol_from_id(PROTO_ID_MPLS)->bytelength;

    INTERPRET_DACH(dach_start);

    return (int)dach.level - (int)mp->level;
}

static unsigned char get_pw_ttl(const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return -1;
    }

    unsigned char *mpls_start = p->buf + p->headers[0].start;
    return mpls_start[3];
}

static struct JsonValue *unpack_tsn_message(const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (eth, vlan), how was this matched??");
        return NULL;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return NULL;
    }

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_ETH)->bytelength +
        protocol_from_id(PROTO_ID_CVLAN)->bytelength;

    unsigned char *eth = p->buf + p->headers[0].start;
    unsigned char *vlan = eth + protocol_from_id(PROTO_ID_ETH)->bytelength;

    unsigned char *cfm = vlan + protocol_from_id(PROTO_ID_CVLAN)->bytelength;
    header_len += protocol_from_id(PROTO_ID_CFM)->bytelength + 3; // added the tlv header

    if (plen < header_len) {
        log_error("TSN OAM packet is too short");
        return NULL;
    }

    unsigned char *rtag = NULL;
    if (vlan[2] == 0xf1 && vlan[3] == 0xc1) {
        rtag = vlan + protocol_from_id(PROTO_ID_CVLAN)->bytelength;
        cfm += protocol_from_id(PROTO_ID_RTAG)->bytelength;
        header_len += protocol_from_id(PROTO_ID_RTAG)->bytelength;
    }

    if (plen < header_len + 1) { // added the end tlv
        log_error("TSN OAM packet is too short");
        return NULL;
    }

    char *json_str = (char*)(cfm + protocol_from_id(PROTO_ID_CFM)->bytelength + 3);
    unsigned json_len = plen - header_len - 1; // don't include the end tlv
    unsigned tlv_len = (cfm[5] << 8) + cfm[6];
    if (json_len != tlv_len) {
        log_error("TSN OAM packet has inconsistent TLV length %u %u", json_len, tlv_len);
    }

    char *jerror;
    struct JsonValue *js = json_parse(json_str, json_len, &jerror);
    if (js == NULL || js->type != JSON_OBJECT) {
        log_error("Received PW OAM packet contains invalid JSON string: %s", jerror);
        free(jerror);
        return NULL;
    }

    int cfm_level = cfm[0] >> 5;
    struct JsonValue *jlevel = json_object_get_any(js, "level");
    if (jlevel) {
        log_warning("Received TSN OAM packet's JSON contains level");
    }
    json_object_insert(js, "level", json_number(cfm_level));

    unsigned nodeid = 0;
    nodeid += eth[6+2] << 24;
    nodeid += eth[6+3] << 16;
    nodeid += eth[6+4] <<  8;
    nodeid += eth[6+5] <<  0;
    struct JsonValue *jnodeid = json_object_get_any(js, "nodeid");
    if (jnodeid) {
        log_warning("Received TSN OAM packet's JSON contains nodeid");
    }
    json_object_insert(js, "nodeid", json_number(nodeid));

    if (rtag) {
        unsigned seq = rtag[1];
        unsigned session = rtag[3] & 0x0f; // upper bits are version

        struct JsonValue *jsse  = json_object_get_any(js, "seq");
        if (jsse) {
            log_warning("Received TSN OAM packet's JSON contains seq");
        }
        jsse  = json_object_get_any(js, "session");
        if (jsse) {
            log_warning("Received TSN OAM packet's JSON contains session");
        }
        json_object_insert(js, "seq", json_number(seq));
        json_object_insert(js, "session", json_number(session));
    }

    return js;
}

static struct JsonValue *pack_tsn_message_header(const struct OAM_MaintenancePoint *mp, struct Packet *p,
        const struct OamRequest *req)
{
    unsigned nodeid;
    unsigned char level;
    unsigned char session;
    unsigned char seq;
    unsigned char ttl;

    request_get_identification_data(req, &nodeid, &level, &session, &seq, &ttl);

    packet_clear_headers(p);
    packet_enlarge_scratch(p);

    packet_add_header(p, 0, PROTO_ID_ETH, protocol_from_id(PROTO_ID_ETH)->bytelength);
    packet_add_header(p, 1, mp->protostack[1], protocol_from_id(mp->protostack[1])->bytelength); // cvlan or svlan

    unsigned char *eth = p->buf + p->headers[0].start;
    memcpy(eth, mp->tsn_address.dmac, 6);
    memset(eth+6, 0, 6);
    eth[6+2] = (nodeid >> 24) & 0xff;
    eth[6+3] = (nodeid >> 16) & 0xff;
    eth[6+4] = (nodeid >>  8) & 0xff;
    eth[6+5] = (nodeid >>  0) & 0xff;
    if (mp->protostack[1] == PROTO_ID_CVLAN) {
        eth[12] = 0x81;
        eth[13] = 0x00;
    } else {
        eth[12] = 0x88;
        eth[13] = 0xa8;
    }

    unsigned char *vlan = p->buf + p->headers[1].start;
    vlan[0] = mp->tsn_address.vlan[0];
    vlan[1] = mp->tsn_address.vlan[1];

    unsigned char *cfm;
    unsigned char *tlv;
    if (mp->protostack[2] == PROTO_ID_RTAG) {
        packet_add_header(p, 2, PROTO_ID_RTAG, protocol_from_id(PROTO_ID_RTAG)->bytelength);
        packet_add_header(p, 3, PROTO_ID_PAYLOAD, 8); // set up cfm and an empty tlv initially

        vlan[2] = 0xf1; // rtag
        vlan[3] = 0xc1;

        unsigned char *rtag = p->buf + p->headers[2].start;
        rtag[0] = 0x10; // indicator nibble and reserved
        rtag[1] = seq;
        rtag[2] = 0; // flags
        rtag[3] = session & 0x0f; // upper bits are version=0
        rtag[4] = 0x89; // cfm
        rtag[5] = 0x02;

        // make sure writeseq works fine
        p->sequence = (rtag[0]<<0) + (rtag[1]<<8) + (rtag[2]<<16) + (rtag[3]<<24);

        cfm = p->buf + p->headers[3].start;
        tlv = cfm + 4;
    } else {
        packet_add_header(p, 2, PROTO_ID_PAYLOAD, 4); // set up cfm and an empty tlv initially

        vlan[2] = 0x89; // cfm
        vlan[3] = 0x02;

        cfm = p->buf + p->headers[2].start;
        tlv = cfm + 4;
    }

    cfm[0] = level << 5;
    cfm[1] = OAM_CFM_REQUEST_OPCODE;
    cfm[2] = 0; // flags
    cfm[3] = 0; // tlv offset

    tlv[0] = 3; // generic data tlv
    tlv[1] = 0; // length hi
    tlv[2] = 0; // length lo
    tlv[3] = 0; // end tlv indicator

    p->ttl = ttl;

    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string(request_get_type(req)));
    json_object_insert(js, "code", json_string("request"));

    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    p->recv_time = sendtime;
    timespec_to_tsntstamp(p->timestamp, &sendtime);

    json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
    json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));

    return js;
}

static bool pack_tsn_payload(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct JsonValue *msg)
{
    if (!packet_is_linear(p)) {
        log_error("can't update message in packet that is not continuous in memory");
        return false;
    }

    struct JsonValue *out = json_duplicate(msg);
    const char *ach_keys[] = { "version", "seq", "channel", "nodeid", "level", "flags", "session" };
    for (unsigned i=0; i<ARRAY_SIZE(ach_keys); i++) {
        json_object_remove(out, ach_keys[i]);
    }

    unsigned js_length;
    char *js_string = json_serialize(out, &js_length);
    if (js_string == NULL) {
        log_error("could not serialize the updated message");
        json_delete(out);
        return false;
    }
    json_delete(out);

    // write the new json string into p
    // TODO this packet manipulation is extremely ugly
    unsigned header_len = 0;
    for (unsigned i=0; mp->protostack[i] != PROTO_ID_PAYLOAD; i++) {
        header_len += protocol_from_id(mp->protostack[i])->bytelength;
    }
    unsigned char *cfm = p->buf + p->headers[0].start + header_len;
    unsigned char *tlv = cfm + 4;
    tlv[1] = (js_length >> 8) & 0xff;
    tlv[2] = js_length & 0xff;
    memcpy(tlv+3, js_string, js_length+1); // also set the closing 0
    free(js_string);
    unsigned new_len = header_len + 4 + 3 + js_length + 1;
    // note: we don't know how many and what type of headers there are in @p
    //       we only know that the packet is linear in memory
    if (p->headers[0].start < p->start) {
        // data is on the scratch
        p->headers[p->header_count-1].len += new_len - p->scratch_len;
        p->scratch_len = new_len;
    } else {
        // data is in the receive area
        p->headers[p->header_count-1].len += new_len - p->len;
        p->len = new_len;
    }
    return true;
}

static int compare_tsn_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (eth, vlan), how was this matched??");
        return -1;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return -1;
    }

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_ETH)->bytelength +
        protocol_from_id(PROTO_ID_CVLAN)->bytelength;

    if (plen < header_len) {
        log_error("TSN OAM packet is too short");
        return -1;
    }

    unsigned char *vlan = p->buf + p->headers[0].start + protocol_from_id(PROTO_ID_ETH)->bytelength;

    unsigned char *cfm = vlan + protocol_from_id(PROTO_ID_CVLAN)->bytelength;
    header_len += protocol_from_id(PROTO_ID_CFM)->bytelength;

    if (vlan[2] == 0xf1 && vlan[3] == 0xc1) {
        cfm += protocol_from_id(PROTO_ID_RTAG)->bytelength;
        header_len += protocol_from_id(PROTO_ID_RTAG)->bytelength;
    }

    if (plen < header_len) {
        log_error("TSN OAM packet is too short");
        return -1;
    }

    int cfm_level = cfm[0] >> 5;
    return cfm_level - (int)mp->level;
}


static void set_mp_address(struct OAM_MaintenancePoint *mp, struct OAM_MP_Address *addr)
{
    //TODO is it possible that we overwrite a valid address with OAM_FROM_Unknown?
    for (struct OAM_MP_Address *ad=addr; ad; ad=ad->next) {
        if (mp->encap == OAM_SRv6) {
            if (strcmp(ad->field, "loc") == 0) {
                mp->sid_address.loc_source = ad->source;
                if (ad->source == OAM_FROM_Edit || ad->source == OAM_FROM_Match)
                    memcpy(mp->sid_address.loc, ad->val.value, 8);
            }
        } else if (mp->encap == OAM_PW) {
            if (strcmp(ad->field, "label") == 0) {
                mp->pw_address.label_source = ad->source;
                if (ad->source == OAM_FROM_Edit || ad->source == OAM_FROM_Match)
                    memcpy(mp->pw_address.label, ad->val.value, 4);
            }
        } else if (mp->encap == OAM_TSN) {
            if (strcmp(ad->field, "dmac") == 0) {
                mp->tsn_address.dmac_source = ad->source;
                if (ad->source == OAM_FROM_Edit || ad->source == OAM_FROM_Match) {
                    // note: we don't have all 6 bytes if it was a prefix match
                    memcpy(mp->tsn_address.dmac, ad->val.value, DIVCEIL(ad->val.bitcount, 8));
                } else if (ad->source == OAM_FROM_Unknown) {
                    mp->tsn_address.dmac_source = OAM_FROM_Default;
                    // group destination address for Continuity Check messages
                    memcpy(mp->tsn_address.dmac, "\x01\x80\xc2\x00\x00", 5);
                    mp->tsn_address.dmac[5] = mp->level;
                }
            } else if (strcmp(ad->field, "vlan") == 0 || strcmp(ad->field, "vid") == 0) {
                mp->tsn_address.vlan_source = ad->source;
                if (ad->source == OAM_FROM_Edit || ad->source == OAM_FROM_Match)
                    memcpy(mp->tsn_address.vlan, ad->val.value, 2);
            }
        }
    }
    if (mp->encap == OAM_SRv6) {
        mp->address_ok = mp->sid_address.loc_source != OAM_FROM_Unknown;
    } else if (mp->encap == OAM_PW) {
        mp->address_ok = mp->pw_address.label_source != OAM_FROM_Unknown;
    } else if (mp->encap == OAM_TSN) {
        mp->address_ok = mp->tsn_address.dmac_source != OAM_FROM_Unknown
                      && mp->tsn_address.vlan_source != OAM_FROM_Unknown;
    }
}


struct OAM_MaintenancePoint *oam_new_maintenance_point(const char *stream_name, const char *mp_name,
        enum OAM_MP_Type type, unsigned level,
        const enum ProtocolID *protostack,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx,
        struct OAM_MP_Address *addr)
{
    struct OAM_MaintenancePoint *mp = NULL;
    if (mp_hash == NULL) {
        mp_hash = new_hashmap(13, mp_delete_cb, NULL);
    } else {
        mp = (struct OAM_MaintenancePoint *)hashmap_find(mp_hash, mp_name);
    }

    if (mp) {
        if (mp->object != obj) {
            if (level != mp->level) {
                log_error("Redefined MP '%s' with level %u previous level %u",
                        mp_name, level, mp->level);
                return NULL;
            }

            if (type != mp->type) {
                log_error("Redefined MP '%s' with type %s previous type %s",
                        mp_name, oam_mp_type_to_str(type), oam_mp_type_to_str(mp->type));
                return NULL;
            }

            if (!obj) {
                log_error("Redefined MP '%s' without object, original object is '%s'",
                        mp_name, pipelineobject_get_name(mp->object));
            } else if (!mp->object) {
                log_error("Redefined MP '%s' with object '%s' conflict with previous definition without object",
                        mp_name, pipelineobject_get_name(obj));
            } else {
                log_error("Redefined MP '%s' with object '%s' previous object '%s'",
                        mp_name, pipelineobject_get_name(obj), pipelineobject_get_name(mp->object));
            }
            return NULL;
        }

        if (strcmp(stream_name, mp->stream_name) != 0) {
            log_error("MP '%s' defined twice, in streams '%s' and '%s'",
                    mp_name, mp->stream_name, stream_name);
            return NULL;
        }

        if (pipe != NULL && mp->pipe == NULL) {
            mp->pipe = pipe;
            mp->pipe_pos_idx = idx;
        }

        set_mp_address(mp, addr);

        if (mp->object && mp->object->type == PO_REPL) {
            if (oam_is_automip_name(mp_name, 1)) {
                if (mp->pipe && mp->pipe->mask) {
                    mp_initiate_mask_signalling(mp, NULL);
                    log_debug("%s started mask signal on refcount increase", mp->name);
                }
            }
        }

        int refcount = __atomic_add_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
        log_debug("%s ref refcount %d", mp_name, refcount);
        return mp;
    }

    //TODO for MIP we first create the receiver that has no pipe pointer
    /*if (type != OAM_Stop) {
        if (pipe == NULL) {
            log_error("%s '%s' needs a pipeline injection point",
                    oam_mp_type_to_str(type), mp_name);
            return NULL;
        }
    }*/

    mp = calloc_struct(OAM_MaintenancePoint);
    mp->name = strdup(mp_name);
    mp->stream_name = strdup(stream_name);
    mp->type = type;
    switch(protostack[0]) {
        case PROTO_ID_ETH: mp->encap = OAM_TSN; break;
        case PROTO_ID_MPLS: mp->encap = OAM_PW; break;
        case PROTO_ID_IPv6: mp->encap = OAM_SRv6; break;
        default:
            log_error("%s '%s' unknown OAM protocol type",
                    oam_mp_type_to_str(type), mp_name);
            return NULL;
    }
    mp->level = level;
    mp->protostack = protostack;

    mp->reference_count = 1;

    mp->pipe = pipe;
    mp->pipe_pos_idx = idx;

    set_mp_address(mp, addr);

    mp->object = obj;
    if (obj) {
        pipeline_object_ref(obj);
        if (obj->type == PO_SEQREC) {
            if (oam_is_automip_name(mp_name, 1)) {
                seq_rec_register_postAutoMIP(obj, mp_name);
            } else if (oam_is_automip_name(mp_name, -1)) {
                seq_rec_register_preAutoMIP(obj, mp_name);
            }
        } else if (obj->type == PO_REPL) {
            if (oam_is_automip_name(mp_name, 1)) {
                // we never get here, because first the reception point is created without pipe...
                if (pipe && pipe->mask) {
                    mp_initiate_mask_signalling(mp, NULL);
                    log_debug("%s started mask signal on creation", mp->name);
                }
            }
        }
    }

    hashmap_insert(mp_hash, mp->name, mp);
    notification_register_source(mp_name, mp_notification_pull_fn, mp, 2000);


    log_debug("%s create refcount 1", mp_name);

    return mp;
}

void oam_unref_maintenance_point(struct OAM_MaintenancePoint *mp)
{
    int refcount = __atomic_sub_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s unref refcount %d", mp->name, refcount);

    if (refcount == 0) {
        hashmap_remove(mp_hash, mp->name);
    }

    if (hashmap_count(mp_hash) == 0) {
        mp_hash = delete_hashmap(mp_hash);
    }
}

struct OAM_MaintenancePoint *find_maintenance_point(const char *name)
{
    if (mp_hash) {
        struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)hashmap_find(mp_hash, name);
        if (mp) {
            int refcount = __atomic_add_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
            log_debug("%s ref refcount %d", name, refcount);
        }
        return mp;
    } else {
        return NULL;
    }
}

char *oam_automip_name(const char *stream, unsigned level, const char *object_name, bool post)
{
    //TODO use a pattern that is impossible to manually create
    return strdup_printf("o_%s_L%u_%s-%s", stream, level, post ? "post" : "pre", object_name);
}

bool oam_is_automip_name(const char *name, int position)
{
    bool is_automip = name[0] == 'o' && name[1] == '_';
    if (is_automip) {
        if (position > 0) {
            const char *post = strstr(name, "_post-");
            return post != NULL;
        } else if (position < 0) {
            const char *pre = strstr(name, "_pre-");
            return pre != NULL;
        }
    }
    return false;
}

const char *oam_mp_type_to_str(enum OAM_MP_Type type)
{
    switch (type) {
        case  OAM_Start:
            return "MEP-Start";
        case OAM_Stop:
            return "MEP-Stop";
        case OAM_Intermediate:
            return "MIP";
    }
    return NULL;
}

const char *oam_mp_encap_to_str(enum OAM_MP_Encap encap)
{
    switch (encap) {
        case OAM_PW:
            return "PseudoWire";
        case OAM_TSN:
            return "TSN";
        case OAM_SRv6:
            return "SRv6";
    }
    return NULL;
}

const char *oam_mp_addr_source_to_str(enum OAM_MP_Addr_Source src)
{
    switch (src) {
        case OAM_FROM_Unknown:
            return "Unknown";
        case OAM_FROM_Edit:
            return "Edit before MP";
        case OAM_FROM_Match:
            return "Match";
        case OAM_FROM_Later:
            return "Edit after MP";
        case OAM_FROM_Default:
            return "Default";
    }
    return NULL;
}

const char *mp_get_name(const struct OAM_MaintenancePoint *mp)
{
    return mp->name;
}

const char *mp_get_stream_name(const struct OAM_MaintenancePoint *mp)
{
    return mp->stream_name;
}

unsigned mp_get_level(const struct OAM_MaintenancePoint *mp)
{
    return mp->level;
}

enum OAM_MP_Type mp_get_type(const struct OAM_MaintenancePoint *mp)
{
    return mp->type;
}

bool mp_can_send(const struct OAM_MaintenancePoint *mp)
{
    return mp->type != OAM_Stop && mp->pipe != NULL && mp->address_ok;
}

struct JsonValue *mp_get_state_json(const struct OAM_MaintenancePoint *mp, bool object_info)
{
    struct JsonValue *ret = json_object();
    json_object_insert(ret, "name", json_string(mp->name));
    json_object_insert(ret, "stream_name", json_string(mp->stream_name));
    json_object_insert(ret, "type", json_string(oam_mp_type_to_str(mp->type)));
    json_object_insert(ret, "level", json_number(mp->level));
    json_object_insert(ret, "send", json_number(mp->oam_send));
    json_object_insert(ret, "recv", json_number(mp->oam_recv));

    if (mp->object) {
        if (object_info) {
            struct JsonValue *objinfo = mp->object->get_state(mp->object);
            json_object_insert(ret, "object", objinfo);
        } else {
            json_object_insert(ret, "object", json_string(pipelineobject_get_name(mp->object)));
        }
    }

    //TODO add mask state

    return ret;
}

struct AddMPState {
    struct JsonValue *jlist;
    struct PipelineObject *object;
};
static int add_mp_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct AddMPState *st = (struct AddMPState *)userdata;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)value;

    if (mp->object == st->object) {
        json_array_push(st->jlist, mp_get_state_json(mp, false));
    }
    return 1;
}

struct JsonValue *mp_get_state_json_by_object(const struct OAM_MaintenancePoint *mp)
{
    struct JsonValue *jlist = json_array();

    if (mp->object) {
        json_array_push(jlist, mp_get_state_json(mp, false));
    } else {
        struct AddMPState st = { jlist, mp->object };
        foreach_mp(false, add_mp_cb, &st);
    }

    return jlist;
}

void mp_print_info(const struct OAM_MaintenancePoint *mp, FILE *out, bool details)
{
    fprintf(out, "%s in %s type %s level %u %s",
            mp->name, mp->stream_name, oam_mp_type_to_str(mp->type), mp->level, oam_mp_encap_to_str(mp->encap));
    if (mp->pipe)
        fprintf(out, " (pipe %s idx %u)%s", mp->pipe->name, mp->pipe_pos_idx, mp_can_send(mp) ? "" : " CAN'T SEND");

    if (details) {
        if (mp->object)
            fprintf(out, "\n    object %s type %s",
                    pipelineobject_get_name(mp->object), pipelineobject_name_from_type(mp->object->type));
        fprintf(out, "\n    send %u recv %u",
                mp->oam_send, mp->oam_recv);
    }
}

void mp_print_mask_signalling_state(const struct OAM_MaintenancePoint *mp, FILE *out)
{
    fprintf(out, "%s %ssending mask signal\n", mp->name, mp->mask_send ? "" : "not ");
}

int foreach_mp(bool sorted, hashmap_cb *cb, void *userdata)
{
    if (sorted)
        return hashmap_foreach_sorted(mp_hash, cb, userdata);
    else
        return hashmap_foreach(mp_hash, cb, userdata);
}

void mp_count_received_message(struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    (void)p;
    __atomic_add_fetch(&mp->oam_recv, 1, __ATOMIC_RELAXED);
}

struct JsonValue *mp_unpack_message(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->encap == OAM_SRv6) {
        return unpack_srv6_message(p);
    }
    if (mp->encap == OAM_PW) {
        return unpack_pw_message(p);
    }
    else if (mp->encap == OAM_TSN) {
        return unpack_tsn_message(p);
    }

    return NULL;
}

struct JsonValue *mp_pack_message_header(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct OamRequest *req)
{
    if (mp->encap == OAM_SRv6) {
        return pack_srv6_message_header(mp, p, req);
    }
    if (mp->encap == OAM_PW) {
        return pack_pw_message_header(mp, p, req);
    }
    else if (mp->encap == OAM_TSN) {
        return pack_tsn_message_header(mp, p, req);
    }

    return NULL;
}

bool mp_pack_message_payload(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct JsonValue *msg)
{
    if (mp->encap == OAM_SRv6) {
        return pack_srv6_payload(p, msg);
    }
    if (mp->encap == OAM_PW) {
        return pack_pw_payload(p, msg);
    }
    else if (mp->encap == OAM_TSN) {
        return pack_tsn_payload(mp, p, msg);
    }

    return false;
}

int mp_compare_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->encap == OAM_SRv6) {
        return compare_srv6_level(mp, p);
    }
    if (mp->encap == OAM_PW) {
        return compare_pw_level(mp, p);
    }
    else if (mp->encap == OAM_TSN) {
        return compare_tsn_level(mp, p);
    }

    return -1;
}

unsigned char mp_get_ttl(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->encap == OAM_SRv6) {
        return get_srv6_ttl(p);
    }
    if (mp->encap == OAM_PW) {
        return get_pw_ttl(p);
    }
    if (mp->encap == OAM_TSN) {
        return 255;
    }

    return 0;
}

void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *p)
{
    if (mp->pipe == NULL) {
        log_error("mp %s can't send without a pipe", mp->name);
        return;
    }
    __atomic_fetch_add(&mp->oam_send, 1, __ATOMIC_RELAXED);
    struct PipelineIterator *pi = new_pipe_iterator(mp->pipe, p);
    //TODO pipe_iterator_inject_at(pi, mp->pipe_pos_idx);
    pi->pos = mp->pipe_pos_idx;
    pipe_iterator_run(pi);
}


static void *send_mask_request_thread(void *arg)
{
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)arg;
    struct OamRequest *mask_req = create_mask_request(mp, "mask");

    while (1) {
        struct Packet *packet = new_packet(NULL);
        struct JsonValue *js = mp_pack_message_header(mp, packet, mask_req);

        json_object_insert(js, "target", json_string("nexthop"));

        if (!mp_pack_message_payload(mp, packet, js)) {
            log_error("couldn't pack request payload");
            json_delete(js);
            delete_packet(packet);
            continue; //TODO exit thread with error?
        }
        json_delete(js);

        mp_inject_packet(mp, packet);
        log_packet("%s sent mask signal", mp->name);

        void *stop_signal = messagequeue_pop(mp->mask_queue, MASK_PERIOD_MS * 1000);

        if (stop_signal) {
            log_info("mask sending thread got stop signal");
            break;
        }
    }

    delete_mask_request(mask_req);

    return NULL;
}

static void *recv_mask_timeout_thread(void *arg)
{
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)arg;

    while (1) {
        void *got_mask = messagequeue_pop(mp->mask_queue, MASK_TIMEOUT_MS * 1000);

        if (got_mask) {
            // got mask signal before timeout, nothing to do
        } else {
            log_packet("%s mask session timeout", mp->name);

            seq_rec_path_unmasked(mp->object, mp->name);

            struct Thread *mask_recv = mp->mask_recv;
            mp->mask_recv = NULL;
            thread_exit(mask_recv);
        }
    }

    return NULL;
}


bool mp_initiate_mask_signalling(struct OAM_MaintenancePoint *mp, FILE *cmd_w)
{
    if (!mp_can_send(mp)) {
        if (cmd_w) fprintf(cmd_w, "Error: can't initiate mask signalling, MIP '%s' can't send\n", mp->name);
        return false;
    }

    if (mp->mask_queue == NULL) {
        mp->mask_queue = new_messagequeue();
    }
    mp->mask_send = thread_launch(send_mask_request_thread, mp, "mask %s", mp->name);

    if (cmd_w) fprintf(cmd_w, "Initiated mask signalling from MIP '%s'\n", mp->name);

    return true;
}

bool mp_stop_mask_signalling(struct OAM_MaintenancePoint *mp, FILE *cmd_w)
{
    if (!mp_can_send(mp)) {
        if (cmd_w) fprintf(cmd_w, "Error: can't stop mask signalling, MIP '%s' can't send\n", mp->name);
        return false;
    }

    if (mp->mask_send == NULL) {
        if (cmd_w) fprintf(cmd_w, "Error: can't stop mask signalling on MIP '%s', it's not running\n", mp->name);
        return false;
    }

    messagequeue_push(mp->mask_queue, mp); // the pointer value doesn't matter just be non-null
    mp->mask_send = thread_join(mp->mask_send);

    if (cmd_w) fprintf(cmd_w, "Stopped mask signalling from MIP '%s'\n", mp->name);

    struct OamRequest *unmask_req = create_mask_request(mp, "unmask");
    struct Packet *packet = new_packet(NULL);
    struct JsonValue *js = mp_pack_message_header(mp, packet, unmask_req);
    json_object_insert(js, "target", json_string("nexthop"));

    if (!mp_pack_message_payload(mp, packet, js)) {
        log_error("couldn't pack request payload");
        json_delete(js);
        delete_packet(packet);
        delete_oam_request(unmask_req);
        return false;
    }
    json_delete(js);
    mp_inject_packet(mp, packet);
    delete_mask_request(unmask_req);
    log_packet("%s sent unmask signal", mp->name);

    return true;
}

void mp_receive_mask_signal(struct OAM_MaintenancePoint *mp)
{
    log_packet("%s received mask signal", mp->name);

    if (mp->object) {
        if (mp->object->type == PO_SEQREC) {
            if (mp->mask_recv) {
                // notify the thread so it doesn't timeout
                // the pointer value doesn't matter just be non-null
                messagequeue_push(mp->mask_queue, mp);
            } else {
                if (mp->mask_queue == NULL) {
                    mp->mask_queue = new_messagequeue();
                }

                if (seq_rec_path_masked(mp->object, mp->name)) {
                    mp->mask_recv = thread_launch(recv_mask_timeout_thread, mp, "maskTO %s", mp_get_name(mp));
                } else {
                    //TODO what?
                }
            }
        } else {
            log_warning("MP %s received mask signal, but the associated object is %s",
                    mp->name, pipelineobject_name_from_type(mp->object->type));
        }
    } else {
        log_warning("MP %s received mask signal, but has no associated object",
                mp->name);
    }
}

void mp_receive_unmask_signal(struct OAM_MaintenancePoint *mp)
{
    log_packet("%s received unmask signal", mp->name);

    if (mp->object) {
        if (mp->object->type == PO_SEQREC) {
            if (mp->mask_recv) {
                if (seq_rec_path_unmasked(mp->object, mp->name)) {
                    mp->mask_recv = thread_stop(mp->mask_recv);
                } else {
                    //TODO what?
                }
            }
        } else {
            log_warning("MP %s received unmask signal, but the associated object is %s",
                    mp->name, pipelineobject_name_from_type(mp->object->type));
        }
    } else {
        log_warning("MP %s received unmask signal, but has no associated object",
                mp->name);
    }
}

void oam_automip_start_mask_session(const char *mip_name)
{
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint*)hashmap_find(mp_hash, mip_name);

    if (mp) {
        mp_initiate_mask_signalling(mp, NULL);
    }
}

void oam_automip_stop_mask_session(const char *mip_name)
{
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint*)hashmap_find(mp_hash, mip_name);

    if (mp) {
        mp_stop_mask_signalling(mp, NULL);
    }
}
