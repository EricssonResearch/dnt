// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "protocol.h"
#include "utils.h"

#include <string.h>

#include <arpa/inet.h> /* htons() */
#include <linux/if_ether.h> /* ETH_P_* */
#include <netinet/in.h> /* IPPROTO_* */

#define ETH_P_FRER 0xf1c1

// compatibility with glibc 2.31 and older
#ifndef IPPROTO_ETHERNET
#define IPPROTO_ETHERNET 143
#endif

//TODO writing these conversion functions is a pain, we need a script to generate them
static bool id_from_ethertype(enum ProtocolID *id, uint16_t nexthdr)
{
#define SET_ID(x) *id = x; return true
    switch (nexthdr) {
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
        case ETH_P_CFM:
            SET_ID(PROTO_ID_CFM);
    }
    return false;
#undef SET_ID
}

static bool ethertype_from_id(uint16_t *nexthdr, enum ProtocolID id)
{
#define SET_TYPE(x) *nexthdr = x; return true
    switch (id) {
        case PROTO_ID_SVLAN:
            SET_TYPE(ETH_P_8021AD);
        case PROTO_ID_CVLAN:
            SET_TYPE(ETH_P_8021Q);
        case PROTO_ID_RTAG:
        case PROTO_ID_TTAG:
        case PROTO_ID_OAMRTAG:
            SET_TYPE(ETH_P_FRER);
        case PROTO_ID_MPLS:
            SET_TYPE(ETH_P_MPLS_UC);
        case PROTO_ID_IPv4:
            SET_TYPE(ETH_P_IP);
        case PROTO_ID_IPv6:
            SET_TYPE(ETH_P_IPV6);
        case PROTO_ID_ARP:
            SET_TYPE(ETH_P_ARP);
        case PROTO_ID_CFM:
            SET_TYPE(ETH_P_CFM);
        case PROTO_ID_PAYLOAD:
        case PROTO_ID_ETH:
        case PROTO_ID_DCW:
        case PROTO_ID_TCW:
        case PROTO_ID_UDP:
        case PROTO_ID_TCP:
        case PROTO_ID_OAM:
        case PROTO_ID_ICMPv4:
        case PROTO_ID_ICMPv6:
            return false;
    }
    return false;
#undef SET_TYPE
}

static bool id_from_ipproto(enum ProtocolID *id, uint16_t proto)
{
#define SET_ID(x) *id = x; return true
    switch (proto) {
        case IPPROTO_IPIP:
            SET_ID(PROTO_ID_IPv4);
        case IPPROTO_IPV6:
            SET_ID(PROTO_ID_IPv6);
        case IPPROTO_UDP:
            SET_ID(PROTO_ID_UDP);
        case IPPROTO_TCP:
            SET_ID(PROTO_ID_TCP);
        case IPPROTO_MPLS:
            SET_ID(PROTO_ID_MPLS);
        case IPPROTO_ETHERNET:
            SET_ID(PROTO_ID_ETH);
        case IPPROTO_ICMP:
            SET_ID(PROTO_ID_ICMPv4);
        case IPPROTO_ICMPV6:
            SET_ID(PROTO_ID_ICMPv6);
    }
    return false;
#undef SET_ID
}

static bool ipproto_from_id(uint16_t *proto, enum ProtocolID id)
{
#define SET_PROTO(x) *proto = x; return true
    switch (id) {
        case PROTO_ID_ETH:
            SET_PROTO(IPPROTO_ETHERNET);
        case PROTO_ID_IPv4:
            SET_PROTO(IPPROTO_IPIP);
        case PROTO_ID_IPv6:
            SET_PROTO(IPPROTO_IPV6);
        case PROTO_ID_UDP:
            SET_PROTO(IPPROTO_UDP);
        case PROTO_ID_TCP:
            SET_PROTO(IPPROTO_TCP);
        case PROTO_ID_MPLS:
            SET_PROTO(IPPROTO_MPLS);
        case PROTO_ID_ICMPv4:
            SET_PROTO(IPPROTO_ICMP);
        case PROTO_ID_ICMPv6:
            SET_PROTO(IPPROTO_ICMPV6);
        case PROTO_ID_PAYLOAD:
        case PROTO_ID_CVLAN:
        case PROTO_ID_SVLAN:
        case PROTO_ID_RTAG:
        case PROTO_ID_TTAG:
        case PROTO_ID_DCW:
        case PROTO_ID_TCW:
        case PROTO_ID_OAM:
        case PROTO_ID_ARP:
        case PROTO_ID_OAMRTAG:
        case PROTO_ID_CFM:
            return false;
    }
    return false;
#undef SET_PROTO
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
    {"dei",   3,  1, FT_NUMBER}, // drop eligible indicator
    {"vid",   4, 12, FT_NUMBER},
    {"tpid", 16, 16, FT_NEXTHEADER},
    {"vlan",  0, 16, FT_NUMBER}, // the whole header at once
};

static const struct ProtocolField rtag_fields[] = {
    {"rt_flag",       4,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    5,  1, FT_NUMBER},
    {"initseq_flag",  6,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"seqnum",       16, 16, FT_NUMBER}, // just the sequence number
    {"tpid",         32, 16, FT_NEXTHEADER}, // next protocol id (ethertype)
    {"seq",           0, 32, FT_TSNSEQ}, // sequence and the flags in reserved
};

static const struct ProtocolField ttag_fields[] = {
    {"rt_flag",       4,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    5,  1, FT_NUMBER},
    {"initseq_flag",  6,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"tstampnum",    11, 21, FT_NUMBER}, // just the timestamp
    {"tpid",         32, 16, FT_NEXTHEADER}, // next protocol id (ethertype)
    {"tstamp",        0, 32, FT_TSNTSTAMP}, // timestamp and the flags in reserved
};

static const struct ProtocolField mpls_fields[] = {
    {"label",  0, 20, FT_NUMBER},
    {"class", 20,  3, FT_NUMBER},
    {"bos",   23,  1, FT_NUMBER}, // bottom-of-stack indicator
    {"ttl",   24,  8, FT_TTL},
};

static const struct ProtocolField dcw_fields[] = {
    {"rt_flag",       4,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    5,  1, FT_NUMBER},
    {"initseq_flag",  6,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"seqnum",       16, 16, FT_NUMBER}, // just the sequence number
    {"seq",           0, 32, FT_TSNSEQ}, // sequence and the flags in reserved
};

static const struct ProtocolField tcw_fields[] = {
    {"rt_flag",       4,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    5,  1, FT_NUMBER},
    {"initseq_flag",  6,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"tstampnum",    11, 21, FT_NUMBER}, // just the timestamp
    {"tstamp",        0, 32, FT_TSNTSTAMP}, // timestamp and the flags in reserved
};

static const struct ProtocolField ipv4_fields[] = {
    {"version",         0,  4, FT_NUMBER}, // should be 4
    {"ihl",             4,  4, FT_NUMBER}, // should be 5
    {"verihl",          0,  8, FT_NUMBER}, // should be 0x45
    {"dscp",            8,  6, FT_NUMBER},
    {"ecn",            14,  2, FT_NUMBER}, // explicit congestion notification
    {"tos",             8,  8, FT_NUMBER}, // type of service
    {"length",         16, 16, FT_NUMBER},
    {"id",             32, 16, FT_NUMBER},
    {"flags",          48,  3, FT_NUMBER},
    {"evil",           48,  1, FT_NUMBER}, // RFC 3514
    {"dontfragment",   49,  1, FT_NUMBER},
    {"morefragments",  50,  1, FT_NUMBER},
    {"fragoffset",     51, 13, FT_NUMBER},
    {"ttl",            64,  8, FT_TTL},
    {"protocol",       72,  8, FT_NEXTHEADER},
    {"checksum",       80, 16, FT_CHECKSUM},
    {"src",            96, 32, FT_IPV4ADDRESS},
    {"dst",           128, 32, FT_IPV4ADDRESS},
};
//TODO IP options how? we must support variable-length headers somehow
//      IGMPv2 seems to use the Router Alert option

static const char *const ipv4_default =
        "\x45\x00\x00\x00"
        "\x00\x00\x00\x00"
        "\x40\x00\x00\x00";

static const struct ProtocolField ipv6_fields[] = {
    {"version",      0,   4, FT_NUMBER}, // should be 6
    {"class",        4,   8, FT_NUMBER},
    {"label",       12,  20, FT_NUMBER},
    {"length",      32,  16, FT_NUMBER},
    {"nextheader",  48,   8, FT_NEXTHEADER},
    {"hoplimit",    56,   8, FT_TTL},
    {"src",         64, 128, FT_IPV6ADDRESS},
    {"dst",        192, 128, FT_IPV6ADDRESS},
    {"loc",        192,  64, FT_NUMBER},  // when dst is a SID, this is SRv6 Locator
    {"func",       256,  16, FT_NUMBER},  // SRv6 Functon
    {"flowid",     272,  20, FT_NUMBER},  // SRv6 flow_id
    {"seq",        292,  28, FT_SRV6SEQ},  // SRv6 seq
};

static const char *const ipv6_default =
    "\x60\x00\x00\x00"
    "\x00\x00\x00\x40";

//TODO IPv6 extension headers? most of them are variable-length...

//TODO theoretically this is variable-length, but in practice it's not
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

static const char *const arp_default =
    "\x00\x01\x08\x00"  // Ethernet, IPv4
    "\x06\x04\x00\x01"; // request by default

static const struct ProtocolField udp_fields[] = {
    {"srcport",   0, 16, FT_NUMBER},
    {"dstport",  16, 16, FT_NUMBER},
    {"length",   32, 16, FT_NUMBER},
    {"checksum", 48, 16, FT_CHECKSUM},
};

static const struct ProtocolField tcp_fields[] = {
    {"srcport",      0, 16, FT_NUMBER},
    {"dstport",     16, 16, FT_NUMBER},
    {"seq",         32, 32, FT_NUMBER},
    {"ack",         64, 32, FT_NUMBER},
    {"dataoffs",    96,  4, FT_NUMBER}, // 5 when no options (counts in 4 octet units)
    {"reserved",   100,  4, FT_NUMBER},
    {"flags",      104,  8, FT_NUMBER},
    {"cwr",        104,  1, FT_NUMBER}, // congestion window reduced
    {"ece",        105,  1, FT_NUMBER}, // ECN echo
    {"urg",        106,  1, FT_NUMBER}, // urgent
    {"ack",        107,  1, FT_NUMBER}, // acknowledgement
    {"psh",        108,  1, FT_NUMBER}, // push
    {"rst",        109,  1, FT_NUMBER}, // reset
    {"syn",        110,  1, FT_NUMBER}, // synchronize
    {"fin",        111,  1, FT_NUMBER}, // finish
    {"windowsize", 112, 16, FT_NUMBER},
    {"checksum",   128, 16, FT_CHECKSUM},
    {"urgentp",    144, 16, FT_NUMBER},
};
//TODO TCP options how? we must support variable-length headers somehow

// DetNet MPLS PW OAM Associated Channel Header (d-ACH)
// RFC 9546
static const struct ProtocolField oam_fields[] = {
    {"oam_nibble",  0,  4, FT_NUMBER}, // must be 1
    {"version",     4,  4, FT_NUMBER}, // should be 0
    {"sequence",    8,  8, FT_NUMBER},
    {"channel",    16, 16, FT_NUMBER}, // https://www.iana.org/assignments/g-ach-parameters/g-ach-parameters.xhtml
    {"nodeid",     32, 20, FT_NUMBER},
    {"level",      52,  3, FT_NUMBER},
    {"flags",      55,  5, FT_NUMBER}, // all reserved
    {"session",    60,  4, FT_NUMBER},
};

static const struct ProtocolField oamrtag_fields[] = {
    {"oam_nibble",  0,  4, FT_NUMBER}, // must be 1
    {"reserved",    4,  4, FT_NUMBER}, // must be 0 (this is where the non-standard flags are in rtag)
    {"sequence",    8,  8, FT_NUMBER},
    {"flags",      16,  8, FT_NUMBER}, // all reserved
    {"version",    24,  4, FT_NUMBER}, // should be 0
    {"session",    28,  4, FT_NUMBER},
};

// common header for 802.1ag Connectivity Fault Management (also ITU-T Y.1731)
static const struct ProtocolField cfm_fields[] = {
    {"mel",        0,  3, FT_NUMBER}, // maintenance endpoint level
    {"version",    3,  5, FT_NUMBER}, // should be 0
    {"opcode",     8,  8, FT_NUMBER},
    {"flags",     16,  8, FT_NUMBER}, // no flags are standardized
    {"tlvoffset", 24,  8, FT_NUMBER}, // length of a fixed header after CFM
};

static const struct ProtocolField icmpv4_fields[] = {
    {"type",        0,  8, FT_NUMBER}, //TODO FT_ENUM?
    {"code",        8,  8, FT_NUMBER}, //TODO FT_ENUM?
    {"checksum",   16, 16, FT_CHECKSUM},

    // Echo Request (type=8)
    // Echo Reply (type=0)
    {"identifier", 32, 16, FT_NUMBER},
    {"sequence",   48, 16, FT_NUMBER},

    //TODO other informational messages?
    //  Router Discovery RFC 1256

    // Destination Unreachable (type=3)
    // Time Exceeded (type=11)
    {"unused",     32, 32, FT_NUMBER},

    // Redirect (type=5)
    {"gateway",    32, 32, FT_IPV4ADDRESS},

    // Parameter Problem (type=12)
    {"pointer",    32,  8, FT_NUMBER},
};

// ICMPv6 protocol fields, including type specific fields
static const struct ProtocolField icmpv6_fields[] = {
    {"type",        0,  8, FT_NUMBER}, //TODO FT_ENUM?
    {"code",        8,  8, FT_NUMBER}, //TODO FT_ENUM?
    {"checksum",   16, 16, FT_CHECKSUM},

    // Echo Request (type=128)
    // Echo Reply (type=129)
    {"identifier", 32, 16, FT_NUMBER},
    {"sequence",   48, 16, FT_NUMBER},

    //TODO other informational messages?
    //  Neighbor Discovery, Multicast Listener etc.

    // Destination Unreachable (type=1)
    // Time Exceeded (type=3)
    {"unused",     32, 32, FT_NUMBER},

    // Packet Too Big (type=2)
    {"mtu",        32, 32, FT_NUMBER},

    // Parameter Problem (type=4)
    {"pointer",    32, 32, FT_NUMBER},
};


// the internal id of the protocols is their index in this array
//TODO autogenerate this list
const struct Protocol protocol_list[] = {
    {"payload", payload_fields, 0, 0, NULL, NULL, NULL, 0},
    {"eth", eth_fields, ARRAY_SIZE(eth_fields), 6+6+2, id_from_ethertype, ethertype_from_id, NULL, 0},
    {"svlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, id_from_ethertype, ethertype_from_id, NULL, 0},
    {"cvlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, id_from_ethertype, ethertype_from_id, NULL, 0},
    // note: we cannot destinguish rtag and ttag by their ethertype,
    //       ACT_DELAY and ACT_ELIM must check the rt_flag
    {"rtag", rtag_fields, ARRAY_SIZE(rtag_fields), 6, id_from_ethertype, ethertype_from_id, NULL, 0},
    {"ttag", ttag_fields, ARRAY_SIZE(ttag_fields), 6, id_from_ethertype, ethertype_from_id, NULL, 0},
    {"mpls", mpls_fields, ARRAY_SIZE(mpls_fields), 4, NULL, NULL, NULL, 0},
    {"dcw", dcw_fields, ARRAY_SIZE(dcw_fields), 4, NULL, NULL, NULL, 0},
    {"tcw", tcw_fields, ARRAY_SIZE(tcw_fields), 4, NULL, NULL, NULL, 0},
    {"ipv4", ipv4_fields, ARRAY_SIZE(ipv4_fields), 20, id_from_ipproto, ipproto_from_id, ipv4_default, 12},
    {"ipv6", ipv6_fields, ARRAY_SIZE(ipv6_fields), 40, id_from_ipproto, ipproto_from_id, ipv6_default, 8},
    {"arp", arp_fields, ARRAY_SIZE(arp_fields), 28, NULL, NULL, arp_default, 8},
    {"udp", udp_fields, ARRAY_SIZE(udp_fields), 8, NULL, NULL, NULL, 0},
    {"tcp", tcp_fields, ARRAY_SIZE(tcp_fields), 20, NULL, NULL, NULL, 0},
    {"oam", oam_fields, ARRAY_SIZE(oam_fields), 8, NULL, NULL, NULL, 0},
    {"oamrtag", oamrtag_fields, ARRAY_SIZE(oamrtag_fields), 4, NULL, NULL, NULL, 0},
    {"cfm", cfm_fields, ARRAY_SIZE(cfm_fields), 4, NULL, NULL, NULL, 0},
    {"icmpv4", icmpv4_fields, ARRAY_SIZE(icmpv4_fields), 8, NULL, NULL, NULL, 0},
    {"icmpv6", icmpv6_fields, ARRAY_SIZE(icmpv6_fields), 8, NULL, NULL, NULL, 0},
};

static const unsigned protocol_count = ARRAY_SIZE(protocol_list);

const struct Protocol *protocol_from_id(enum ProtocolID id)
{
    return &protocol_list[id];
}

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
        case FT_SRV6SEQ:
            return "SRv6Seq";
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

bool protocol_type_valid(const char *type)
{
    if (type == NULL) return false;
    for (unsigned i=0; i<protocol_count; i++) {
        if (strcmp(type, protocol_list[i].name) == 0) return true;
    }
    return false;
}

enum ProtocolID protocol_id_from_type(const char *type)
{
    if (type == NULL) return PROTO_ID_PAYLOAD;
    for (unsigned i=0; i<protocol_count; i++) {
        if (strcmp(type, protocol_list[i].name) == 0) return (enum ProtocolID)i;
    }
    return PROTO_ID_PAYLOAD;
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
    if (id < 0 && id >= protocol_count) return NULL;

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
