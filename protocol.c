// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "protocol.h"
#include "utils.h"

#include <string.h>

#include <arpa/inet.h> /* htons() */
#include <linux/if_ether.h> /* ETH_P_* */

#define ETH_P_FRER 0xf1c1

//TODO writing these conversion functions is a pain, we need a script to generate them
static bool id_from_ethertype(enum ProtocolID *id, uint16_t nexthdr)
{
#define SET_ID(x)       \
    do {                \
        *id = x;        \
        return true;    \
    } while (0);

    switch (ntohs(nexthdr)) {
        case ETH_P_8021Q:
            SET_ID(PROTO_ID_CVLAN);
        case ETH_P_8021AD:
            SET_ID(PROTO_ID_SVLAN);
        case ETH_P_FRER:
            SET_ID(PROTO_ID_RTAG);
        case ETH_P_MPLS_UC:
            SET_ID(PROTO_ID_MPLS);
        case ETH_P_IP:
            SET_ID(PROTO_ID_IPv4);
        case ETH_P_IPV6:
            SET_ID(PROTO_ID_IPv6);
        case ETH_P_ARP:
            SET_ID(PROTO_ID_ARP);
    }
    return false;
#undef SET_ID
}

static bool ethertype_from_id(uint16_t *nexthdr, enum ProtocolID id)
{
#define SET_TYPE(x)             \
    do {                        \
        *nexthdr = htons(x);    \
        return true;            \
    } while (0)

    switch (id) {
        case PROTO_ID_SVLAN:
            SET_TYPE(ETH_P_8021AD);
        case PROTO_ID_CVLAN:
            SET_TYPE(ETH_P_8021Q);
        case PROTO_ID_RTAG:
        case PROTO_ID_TTAG:
            SET_TYPE(ETH_P_FRER);
        case PROTO_ID_MPLS:
            SET_TYPE(ETH_P_MPLS_UC);
        case PROTO_ID_IPv4:
            SET_TYPE(ETH_P_IP);
        case PROTO_ID_IPv6:
            SET_TYPE(ETH_P_IPV6);
        case PROTO_ID_ARP:
            SET_TYPE(ETH_P_ARP);
        case PROTO_ID_PAYLOAD:
        case PROTO_ID_ETH:
        case PROTO_ID_DCW:
        case PROTO_ID_TCW:
        case PROTO_ID_UDP:
        case PROTO_ID_OAM:
            return false;
    }
    return false;
#undef SET_TYPE
}

static const struct ProtocolField payload_fields[] = {
};

static const struct ProtocolField eth_fields[] = {
    {"dmac",         0, 6*8, FT_MACADDRESS},
    {"smac",       6*8, 6*8, FT_MACADDRESS},
    {"ethertype", 12*8, 2*8, FT_NEXTHEADER},
};

static const struct ProtocolField vlan_fields[] = {
    {"pcp",   0,  3, FT_NUMBER},
    {"dei",   3,  1, FT_NUMBER},
    {"vid",   4, 12, FT_NUMBER},
    {"tpid", 16, 16, FT_NEXTHEADER},
    {"vlan",  0, 16, FT_NUMBER}, // the whole header at once
};

static const struct ProtocolField rtag_fields[] = {
    {"rt_flag",       5,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    6,  1, FT_NUMBER},
    {"initseq_flag",  7,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"seqnum",       16, 16, FT_NUMBER}, // just the sequence number
    {"tpid",         32, 16, FT_NEXTHEADER}, // next protocol id (ethertype)
    {"seq",           0, 32, FT_TSNSEQ}, // sequence and the flags in reserved
};

static const struct ProtocolField ttag_fields[] = {
    {"rt_flag",       5,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    6,  1, FT_NUMBER},
    {"initseq_flag",  7,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"tstampnum",    11, 21, FT_NUMBER}, // just the timestamp
    {"tpid",         32, 16, FT_NEXTHEADER}, // next protocol id (ethertype)
    {"tstamp",        0, 32, FT_TSNTSTAMP}, // timestamp and the flags in reserved
};

static const struct ProtocolField mpls_fields[] = {
    {"label",  0, 20, FT_NUMBER},
    {"class", 20,  3, FT_NUMBER},
    {"bos",   23,  1, FT_NUMBER},
    {"ttl",   24,  8, FT_TTL},
};

static const struct ProtocolField dcw_fields[] = {
    {"rt_flag",       5,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    6,  1, FT_NUMBER},
    {"initseq_flag",  7,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"seqnum",       16, 16, FT_NUMBER}, // just the sequence number
    {"seq",           0, 32, FT_TSNSEQ}, // sequence and the flags in reserved
};

static const struct ProtocolField tcw_fields[] = {
    {"rt_flag",       5,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    6,  1, FT_NUMBER},
    {"initseq_flag",  7,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"tstampnum",    11, 21, FT_NUMBER}, // just the timestamp
    {"tstamp",        0, 32, FT_TSNTSTAMP}, // timestamp and the flags in reserved
};

static const struct ProtocolField ipv4_fields[] = {
    {"version",         0,  4, FT_NUMBER},
    {"ihl",             4,  4, FT_NUMBER},
    {"dscp",            8,  6, FT_NUMBER},
    {"ecn",            14,  2, FT_NUMBER},
    {"tos",             8,  8, FT_NUMBER},
    {"length",         16, 16, FT_NUMBER},
    {"id",             32, 16, FT_NUMBER},
    {"flags",          48,  3, FT_NUMBER},
    {"dontfragment",   49,  1, FT_NUMBER},
    {"morefragments",  50,  1, FT_NUMBER},
    {"fragoffset",     51, 13, FT_NUMBER},
    {"ttl",            64,  8, FT_TTL},
    {"protocol",       72,  8, FT_NEXTHEADER},
    {"checksum",       80, 16, FT_CHECKSUM},
    {"src",            96, 32, FT_IPV4ADDRESS},
    {"dst",           128, 32, FT_IPV4ADDRESS},
};

static const struct ProtocolField ipv6_fields[] = {
    {"version",      0,   4, FT_NUMBER},
    {"class",        4,   8, FT_NUMBER},
    {"label",       12,  20, FT_NUMBER},
    {"length",      32,  16, FT_NUMBER},
    {"nextheader",  48,   8, FT_NEXTHEADER},
    {"hoplimit",    56,   8, FT_TTL},
    {"src",         64, 128, FT_IPV6ADDRESS},
    {"dst",        192, 128, FT_IPV6ADDRESS},
};

//TODO IPv6 extension headers?

static const struct ProtocolField arp_fields[] = {
    {"hwtype",   0, 16, FT_NUMBER}, // should be 1 (Eth)
    {"prtype",  16, 16, FT_NUMBER}, // should be 0x0800 (IPv4)
    {"hwsize",  32,  8, FT_NUMBER}, // should be 6
    {"prsize",  40,  8, FT_NUMBER}, // should be 4
    {"opcode",  48, 16, FT_NUMBER}, // 1 request, 2 reply
    {"srcmac",  64, 48, FT_MACADDRESS},
    {"srcip",  112, 32, FT_IPV4ADDRESS},
    {"dstmac", 144, 48, FT_MACADDRESS},
    {"dstip",  192, 32, FT_IPV4ADDRESS},
};

static const struct ProtocolField udp_fields[] = {
    {"srcport",   0, 16, FT_NUMBER},
    {"dstport",  16, 16, FT_NUMBER},
    {"length",   32, 16, FT_NUMBER},
    {"checksum", 48, 16, FT_CHECKSUM},
};

// MPLS OAM Associated Channel Header (ACH)
static const struct ProtocolField oam_fields[] = {
    {"oam_nibble",  0,  4, FT_NUMBER}, // must be 1
    {"version",     4,  4, FT_NUMBER},
    {"sequence",    8,  8, FT_NUMBER},
    {"channel",    16, 16, FT_NUMBER}, // https://www.iana.org/assignments/g-ach-parameters/g-ach-parameters.xhtml
    {"nodeid",     32, 20, FT_NUMBER},
    {"level",      52,  3, FT_NUMBER},
    {"flags",      55,  5, FT_NUMBER}, // all reserved
    {"session",    60,  4, FT_NUMBER},
};

//TODO autogenerate this list
const struct Protocol protocol_list[] = {
    {"payload", payload_fields, 0, 0, 0, NULL, NULL},
    {"eth", eth_fields, ARRAY_SIZE(eth_fields), 6+6+2, 2, id_from_ethertype, ethertype_from_id},
    {"svlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, 3, id_from_ethertype, ethertype_from_id},
    {"cvlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, 3, id_from_ethertype, ethertype_from_id},
    // note: we cannot destinguish rtag and ttag by their ethertype,
    //       ACT_DELAY and ACT_ELIM must check the rt_flag
    {"rtag", rtag_fields, ARRAY_SIZE(rtag_fields), 6, 5, id_from_ethertype, ethertype_from_id},
    {"ttag", ttag_fields, ARRAY_SIZE(ttag_fields), 6, 5, id_from_ethertype, ethertype_from_id},
    {"mpls", mpls_fields, ARRAY_SIZE(mpls_fields), 4, 0, NULL, NULL},
    {"dcw", dcw_fields, ARRAY_SIZE(dcw_fields), 4, 0, NULL, NULL},
    {"tcw", tcw_fields, ARRAY_SIZE(tcw_fields), 4, 0, NULL, NULL},
    {"ipv4", ipv4_fields, ARRAY_SIZE(ipv4_fields), 20, 0, NULL, NULL}, //TODO protocol field
    {"ipv6", ipv6_fields, ARRAY_SIZE(ipv6_fields), 40, 0, NULL, NULL}, //TODO next header field
    {"arp", arp_fields, ARRAY_SIZE(arp_fields), 28, 0, NULL, NULL}, //TODO this is variable-length
    {"udp", udp_fields, ARRAY_SIZE(udp_fields), 8, 0, NULL, NULL},
    {"oam", oam_fields, ARRAY_SIZE(oam_fields), 4, 0, NULL, NULL},
};

const unsigned protocol_count = ARRAY_SIZE(protocol_list);


const char *fieldtype_name_from_type(enum ProtocolFieldType type)
{
    switch (type) {
        case FT_UNKNOWN:
            return "Unknown";
        case FT_NUMBER:
            return "Number";
        case FT_MACADDRESS:
            return "MAC";
        case FT_IPV4ADDRESS:
            return "IPv4";
        case FT_IPV6ADDRESS:
            return "IPv6";
        case FT_TSNSEQ:
            return "TSNSeq";
        case FT_TSNTSTAMP:
            return "TSNTstamp";
        case FT_TTL:
            return "TTL";
        case FT_CHECKSUM:
            return "Checksum";
        case FT_NEXTHEADER:
            return "NextHeader";
    }
    return NULL;
}

enum ProtocolID protocol_id_from_type(const char *type)
{
    if (type == NULL) return -1;
    for (unsigned i=0; i<protocol_count; i++) {
        if (strcmp(type, protocol_list[i].name) == 0) return i;
}
    return -1;
}

const char *protocol_type_from_id(enum ProtocolID id)
{
    if (id >=0 && id < protocol_count) {
        return protocol_list[id].name;
    }
    return NULL;
}

const struct ProtocolField *protocol_get_field_by_name(enum ProtocolID id, const char *fieldname)
{
    if (id < 0 && id >= protocol_count) return false;

    for (unsigned i=0; i<protocol_list[id].header_field_count; i++) {
        if (strcmp(fieldname, protocol_list[id].header_fields[i].name) == 0)
            return &protocol_list[id].header_fields[i];
    }
    return NULL;
}

bool protocol_fieldname_valid(enum ProtocolID id, const char *fieldname)
{
    const struct ProtocolField *f = protocol_get_field_by_name(id, fieldname);
    return f != NULL;
}

const struct ProtocolField *protocol_get_field_by_type(enum ProtocolID id, enum ProtocolFieldType type)
{
    for (unsigned i=0; i<protocol_list[id].header_field_count; i++) {
        if (protocol_list[id].header_fields[i].type == type)
            return &protocol_list[id].header_fields[i];
    }
    return NULL;
}

int protocol_get_field_idx_by_type(enum ProtocolID id, enum ProtocolFieldType type)
{
    for (unsigned i=0; i<protocol_list[id].header_field_count; i++) {
        if (protocol_list[id].header_fields[i].type == type)
            return i;
    }
    return -1;
}
