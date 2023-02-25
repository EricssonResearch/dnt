
#include "protocol.h"
#include "utils.h"

#include <string.h>

#include <arpa/inet.h> /* htons() */
#include <linux/if_ether.h> /* ETH_P_* */

//TODO writing these conversion functions is a pain, we need a script to generate them
static int id_from_ethertype(uint16_t nexthdr)
{
    switch (ntohs(nexthdr)) {
        case ETH_P_8021Q:
            return 2;
        case ETH_P_8021AD:
            return 1;
        case 0xf1c1: // FRER
            return 3;
        //TODO more
    }
    return -1;
}

static uint16_t ethertype_from_id(int id)
{
    uint16_t ret = 0;
    switch (id) {
        case 1:
            ret = ETH_P_8021AD;
            break;
        case 2:
            ret = ETH_P_8021Q;
            break;
        case 3:
        case 4:
            ret = 0xf1c1;
            break;
        //TODO more
    }
    return htons(ret);
}

static struct ProtocolField eth_fields[] = {
    {"dmac",         0, 6*8},
    {"smac",       6*8, 6*8},
    {"ethertype", 12*8, 2*8},
};

static struct ProtocolField vlan_fields[] = {
    {"pcp",   0,  3},
    {"dei",   3,  1},
    {"vid",   4, 12},
    {"tpid", 16, 16},
};

static struct ProtocolField rttag_fields[] = {
    {"rt_flag",       5,  1}, // rtag-ttag indicator
    {"reset_flag",    6,  1},
    {"initseq_flag",  7,  1},
    {"resv",          0, 16}, // reserved
    {"seqnum",       16, 16}, // just the sequence number
    {"seq",           0, 32}, // sequence and the flags in reserve
    {"tstamp",       11, 21}, // timestamp (in ttag)
    {"tpid",         32, 16}, // next protocol id (ethertype)
};

struct Protocol protocol_list[] = {
    //TODO have a dummy in the first place so eth is not 0
    {"eth", eth_fields, ARRAY_SIZE(eth_fields), 6+6+2, "ethertype", id_from_ethertype, ethertype_from_id},
    {"svlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, "tpid", id_from_ethertype, ethertype_from_id},
    {"cvlan", vlan_fields, ARRAY_SIZE(vlan_fields), 4, "tpid", id_from_ethertype, ethertype_from_id},
    {"rtag", rttag_fields, ARRAY_SIZE(rttag_fields), 6, "tpid", id_from_ethertype, ethertype_from_id},
    {"ttag", rttag_fields, ARRAY_SIZE(rttag_fields), 6, "tpid", id_from_ethertype, ethertype_from_id},
};

unsigned protocol_count = ARRAY_SIZE(protocol_list);

int protocol_id_from_type(const char *type)
{
    for (unsigned i=0; i<protocol_count; i++) {
        if (strcmp(type, protocol_list[i].name) == 0) return i;
}
    return -1;
}

const char *protocol_name_from_id(int id)
{
    if (id >=0 && id < (int)protocol_count) {
        return protocol_list[id].name;
    }
    return NULL;
}

bool protocol_fieldname_valid(int id, const char *fieldname)
{
    if (id < 0 && id >= (int)protocol_count) return false;

    for (unsigned i=0; i<protocol_list[id].header_field_count; i++) {
        if (strcmp(fieldname, protocol_list[id].header_fields[i].name) == 0)
            return true;
    }
    return false;
}
