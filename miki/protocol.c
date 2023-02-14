
#include "protocol.h"
#include "utils.h"

#include <string.h>

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
    {"rtflag",  0,  1}, //TODO is this position okay? the old code uses the 5th bit
    {"resv",    0, 16},
    {"seq",    16, 16},
    {"tstamp", 11, 21},
    {"tpid",   32, 16},
};

struct Protocol protocol_list[] = {
    {"eth", eth_fields, ARRAY_SIZE(eth_fields)},
    {"svlan", vlan_fields, ARRAY_SIZE(vlan_fields)},
    {"cvlan", vlan_fields, ARRAY_SIZE(vlan_fields)},
    {"rtag", rttag_fields, ARRAY_SIZE(rttag_fields)},
    {"ttag", rttag_fields, ARRAY_SIZE(rttag_fields)},
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
