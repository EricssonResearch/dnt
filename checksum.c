// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "checksum.h"
#include "inet_utils.h"
#include "interface.h"
#include "log.h"
#include "utils.h"

#include <arpa/inet.h>

DEFAULT_LOGGING_MODULE(CHECKSUM, INFO);

static uint16_t payload_length(struct Packet *p, struct ChecksumParameters *cp)
{
    uint8_t *iph = p->buf + p->headers[cp->ip_idx].start;
    uint16_t len = 0;
    if (cp->ip_version == PROTO_ID_IPv4) {
        len = (iph[2] << 8) + iph[3];
        //TODO how do we handle IP header length not being 5?
        unsigned iplen = (iph[0] & 0x0f) * 4;
        len -= iplen;
    } else {
        len = (iph[4] << 8) + iph[5];
    }
    log_debug("packet %u len from ip %u", p->id, len);

    // ignore the size of the headers between ip and icmp
    for (unsigned i=cp->ip_idx+1; i<cp->hdr_idx; i++) {
        len -= p->headers[cp->hdr_idx].len;
    }
    log_debug("packet %u corrected len %u", p->id, len);
    return len;
}

static uint16_t ipv4_header_csum(uint8_t *iph)
{
    // just self
    unsigned iplen = (iph[0] & 0x0f) * 4;
    uint32_t csum32 = csum_partial(iph, iplen, 0);
    return csum_fold(csum32);
}

static uint16_t icmpv4_csum(struct Packet *p, struct ChecksumParameters *cp)
{
    // self + payload
    uint16_t len = payload_length(p, cp);
    uint32_t csum32 = 0;

    for (unsigned i=cp->hdr_idx; i<p->header_count; i++) {
        unsigned hlen = MIN(len, p->headers[i].len);
        csum32 = csum_partial(p->buf + p->headers[i].start, hlen, csum32);
        len -= hlen;
    }
    return csum_fold(csum32);
}

static uint16_t layer4_csum(struct Packet *p, struct ChecksumParameters *cp)
{
    // ip pseudo + self + payload
    uint8_t protoid = 0;
    if (cp->hdr_proto == PROTO_ID_ICMPv6) {
            protoid = IPPROTO_ICMPV6;
        } else if (cp->hdr_proto == PROTO_ID_TCP) {
            protoid = IPPROTO_TCP;
        } else if (cp->hdr_proto == PROTO_ID_UDP) {
            protoid = IPPROTO_UDP;
        } else {
            log_error("don't know how to checksum %s", protocol_from_id(cp->hdr_proto)->name);
            return 0xffff;
        }

        uint16_t len = payload_length(p, cp);
        uint32_t csum32 = 0;

        // ip pseudo header
        uint8_t *iph = p->buf + p->headers[cp->ip_idx].start;
        if (cp->ip_version == PROTO_ID_IPv4) {
            csum32 = csum_partial(iph+12, 8, csum32); // addresses
            uint8_t pseudo[4];
            pseudo[0] = 0;
            pseudo[1] = protoid;
            pseudo[2] = (len >> 8) & 0xff;
            pseudo[3] = (len >> 0) & 0xff;
            csum32 = csum_partial(pseudo, 4, csum32);
        } else {
            csum32 = csum_partial(iph+8, 32, csum32); // addresses
            uint8_t pseudo[8];
            pseudo[0] = 0;
            pseudo[1] = 0;
            pseudo[2] = (len >> 8) & 0xff;
            pseudo[3] = (len >> 0) & 0xff;
            pseudo[4] = 0;
            pseudo[5] = 0;
            pseudo[6] = 0;
            pseudo[7] = protoid;
            csum32 = csum_partial(pseudo, 8, csum32);
        }
        for (unsigned i=cp->hdr_idx; i<p->header_count; i++) {
            unsigned hlen = MIN(len, p->headers[i].len);
            csum32 = csum_partial(p->buf + p->headers[i].start, hlen, csum32);
            len -= hlen;
        }

        return csum_fold(csum32);
}

void checksum_compute(struct Packet *p, struct ChecksumParameters *cp)
{
    if (cp->hdr_proto == PROTO_ID_IPv4) {
        uint8_t *iph = p->buf + p->headers[cp->hdr_idx].start;
        iph[10] = 0;
        iph[11] = 0;

        uint16_t csum16 = ipv4_header_csum(iph);
        log_debug("packet %u ipv4 csum16 0x%.04x", p->id, csum16);

        iph[10] = (csum16 >> 8) & 0xff;
        iph[11] = (csum16 >> 0) & 0xff;
    } else if (cp->hdr_proto == PROTO_ID_ICMPv4) {
        uint8_t *icmph = p->buf + p->headers[cp->hdr_idx].start;
        icmph[2] = 0;
        icmph[3] = 0;

        uint16_t csum16 = icmpv4_csum(p, cp);
        log_debug("packet %u icmpv4 csum16 0x%.04x", p->id, csum16);

        icmph[2] = (csum16 >> 8) & 0xff;
        icmph[3] = (csum16 >> 0) & 0xff;
    } else {
        uint8_t *selfh = p->buf + p->headers[cp->hdr_idx].start;
        uint8_t *csumpos = selfh;
        if (cp->hdr_proto == PROTO_ID_ICMPv6) {
            csumpos = selfh + 2;
        } else if (cp->hdr_proto == PROTO_ID_TCP) {
            csumpos = selfh + 16;
        } else if (cp->hdr_proto == PROTO_ID_UDP) {
            csumpos = selfh + 6;
        } else {
            log_error("don't know how to checksum %s", protocol_from_id(cp->hdr_proto)->name);
            return;
        }
        csumpos[0] = 0;
        csumpos[1] = 0;

        uint16_t csum16 = layer4_csum(p, cp);
        log_debug("packet %u %s csum16 0x%.04x", p->id, protocol_from_id(cp->hdr_proto)->name, csum16);

        csumpos[0] = (csum16 >> 8) & 0xff;
        csumpos[1] = (csum16 >> 0) & 0xff;
    }
}


bool checksum_verify(struct Packet *p, struct ChecksumParameters *cp)
{
    uint16_t csum16 = 0;

    if (cp->hdr_proto == PROTO_ID_IPv4) {
        uint8_t *iph = p->buf + p->headers[cp->hdr_idx].start;
        csum16 = ipv4_header_csum(iph);
        log_debug("packet %u ipv4 csum16 0x%.04x", p->id, csum16);
    } else if (cp->hdr_proto == PROTO_ID_ICMPv4) {
        csum16 = icmpv4_csum(p, cp);
        log_debug("packet %u icmpv4 csum16 0x%.04x", p->id, csum16);
    } else {
        csum16 = layer4_csum(p, cp);
        log_debug("packet %u %s csum16 0x%.04x", p->id, protocol_from_id(cp->hdr_proto)->name, csum16);
    }

    if (csum16 == 0) {
        return true;
    } else {
        if (p->from) {
             __atomic_add_fetch(&p->from->checksum_errors, 1, __ATOMIC_RELAXED);
        }
        return false;
    }
}
