
#include "protocol.h"
#include "utils.h"

#include <string.h>

#include <arpa/inet.h> /* htons() */
#include <linux/if_ether.h> /* ETH_P_* */

#define ETH_P_FRER 0xf1c1

//TODO writing these conversion functions is a pain, we need a script to generate them
static bool id_from_ethertype(int *id, uint16_t nexthdr)
{
    switch (ntohs(nexthdr)) {
        case ETH_P_8021Q:
            *id = PROTO_ID_CVLAN;
            break;
        case ETH_P_8021AD:
            *id = PROTO_ID_SVLAN;
            break;
        case ETH_P_FRER:
            *id = PROTO_ID_RTAG;
            break;
        case ETH_P_MPLS_UC:
            *id = PROTO_ID_MPLS;
            break;
        //TODO more
        default:
            return false;
    }
    return true;
}

static bool ethertype_from_id(uint16_t *nexthdr, int id)
{
    uint16_t ret = 0;
    switch (id) {
        case PROTO_ID_SVLAN:
            ret = ETH_P_8021AD;
            break;
        case PROTO_ID_CVLAN:
            ret = ETH_P_8021Q;
            break;
        case PROTO_ID_RTAG:
        case PROTO_ID_TTAG:
            ret = ETH_P_FRER;
            break;
        case PROTO_ID_MPLS:
            ret = ETH_P_MPLS_UC;
            break;
        //TODO more
        default:
            return false;
    }
    *nexthdr = htons(ret);
    return true;
}

static const struct ProtocolField payload_fields[] = {
};

static const struct ProtocolField eth_fields[] = {
    {"dmac",         0, 6*8, FT_MACADDRESS},
    {"smac",       6*8, 6*8, FT_MACADDRESS},
    {"ethertype", 12*8, 2*8, FT_NUMBER},
};

static const struct ProtocolField vlan_fields[] = {
    {"pcp",   0,  3, FT_NUMBER},
    {"dei",   3,  1, FT_NUMBER},
    {"vid",   4, 12, FT_NUMBER},
    {"tpid", 16, 16, FT_NUMBER},
    {"vlan",  0, 16, FT_NUMBER}, // the whole header at once
};

//TODO it's better if we separate the rtag and ttag format
//      don't have FT_TSNSEQ and FT_TSNTSTAMP in the same header
//      what should id_from_ethertype return? rtag, because that is the standard one
static const struct ProtocolField rttag_fields[] = {
    {"rt_flag",       5,  1, FT_NUMBER}, // rtag-ttag indicator
    {"reset_flag",    6,  1, FT_NUMBER},
    {"initseq_flag",  7,  1, FT_NUMBER},
    {"resv",          0, 16, FT_NUMBER}, // reserved bits
    {"seqnum",       16, 16, FT_NUMBER}, // just the sequence number
    {"tstampnum",    11, 21, FT_NUMBER}, // just the timestamp
    {"tpid",         32, 16, FT_NUMBER}, // next protocol id (ethertype)
    {"seq",           0, 32, FT_TSNSEQ}, // sequence and the flags in reserved
    {"tstamp",        0, 32, FT_TSNTSTAMP}, // timestamp and the flags in reserved
};

static const struct ProtocolField mpls_fields[] = {
    {"label",  0, 20, FT_NUMBER},
    {"class", 20,  3, FT_NUMBER},
    {"bos",   23,  1, FT_NUMBER},
    {"ttl",   24,  8, FT_NUMBER}, //TODO FT_TTL
};

//TODO autogenerate this list
const struct Protocol protocol_list[] = {
    {"payload", payload_fields, 0, 0, 0, NULL, NULL},
    {"eth", eth_fields, ARRAY_SIZE(eth_fields), 6+6+2, 2, id_from_ethertype, ethertype_from_id},
    {"svlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, 3, id_from_ethertype, ethertype_from_id},
    {"cvlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, 3, id_from_ethertype, ethertype_from_id},
    // note: we cannot destinguish rtag and ttag by their ethertype,
    //       ACT_DELAY and ACT_ELIM must check the rt_flag
    {"rtag", rttag_fields, ARRAY_SIZE(rttag_fields), 6, 6, id_from_ethertype, ethertype_from_id},
    {"ttag", rttag_fields, ARRAY_SIZE(rttag_fields), 6, 6, id_from_ethertype, ethertype_from_id},
    {"mpls", mpls_fields, ARRAY_SIZE(mpls_fields), 4, 0, NULL, NULL},
};

unsigned protocol_count = ARRAY_SIZE(protocol_list);


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
    }
    return NULL;
}

int protocol_id_from_type(const char *type)
{
    for (unsigned i=0; i<protocol_count; i++) {
        if (strcmp(type, protocol_list[i].name) == 0) return i;
}
    return -1;
}

const char *protocol_type_from_id(int id)
{
    if (id >=0 && id < (int)protocol_count) {
        return protocol_list[id].name;
    }
    return NULL;
}

const struct ProtocolField *protocol_get_field_by_name(int id, const char *fieldname)
{
    if (id < 0 && id >= (int)protocol_count) return false;

    for (unsigned i=0; i<protocol_list[id].header_field_count; i++) {
        if (strcmp(fieldname, protocol_list[id].header_fields[i].name) == 0)
            return &protocol_list[id].header_fields[i];
    }
    return NULL;
}

bool protocol_fieldname_valid(int id, const char *fieldname)
{
    const struct ProtocolField *f = protocol_get_field_by_name(id, fieldname);
    return f != NULL;
}
