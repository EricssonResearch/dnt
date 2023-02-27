
#ifndef R2_PROTOCOL_H
#define R2_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

struct ProtocolField {
    const char *name;
    unsigned bitoffset;
    unsigned bitcount;
};

// @nexthdr is in network byte order
typedef int id_from_nexthdr(uint16_t nexthdr);

// @returns a number in network byte order
typedef uint16_t nexthdr_from_id(int id);

struct Protocol {
    const char *name;
    struct ProtocolField *header_fields;
    unsigned header_field_count;
    unsigned bytelength;
    const char *nexthdr;
    id_from_nexthdr *get_id;
    nexthdr_from_id *get_nexthdr;
};


// the internal id of the protocols is their index in this array
extern struct Protocol protocol_list[];
extern unsigned protocol_count;

//TODO autogenerate this list
#define PROTO_ID_ETH 1
#define PROTO_ID_SVLAN 2
#define PROTO_ID_CVLAN 3
#define PROTO_ID_RTAG 4
#define PROTO_ID_TTAG 5

// @returns -1 if unknown protocol type
int protocol_id_from_type(const char *type);

// @returns NULL if unknown id
const char *protocol_name_from_id(int id);

// @returns true if @fieldname is valid for this protocol
bool protocol_fieldname_valid(int id, const char *fieldname);

#endif // R2_PROTOCOL_H
