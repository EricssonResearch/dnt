// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_PROTOCOL_H
#define R2_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

enum ProtocolFieldType {
    FT_UNKNOWN,
    FT_NUMBER,
    FT_MACADDRESS,
    FT_IPV4ADDRESS,
    FT_IPV6ADDRESS,
    FT_TSNSEQ,
    FT_TSNTSTAMP,
    FT_TTL,
    FT_CHECKSUM,
    FT_NEXTHEADER,
};

// the internal id of the protocols is their index in the @protocol_list array
//TODO autogenerate this list
enum ProtocolID {
    PROTO_ID_PAYLOAD,
    PROTO_ID_ETH,
    PROTO_ID_SVLAN,
    PROTO_ID_CVLAN,
    PROTO_ID_RTAG,
    PROTO_ID_TTAG,
    PROTO_ID_MPLS,
    PROTO_ID_DCW,
    PROTO_ID_TCW,
    PROTO_ID_IPv4,
    PROTO_ID_IPv6,
    PROTO_ID_ARP,
    PROTO_ID_UDP,
    PROTO_ID_OAM,
};

// describes one field of a protocol header
struct ProtocolField {
    const char *name;
    unsigned bitoffset;
    unsigned bitcount;
    enum ProtocolFieldType type;
};

/*TODO nexthdr<-->id conversion problem:
 * the API is tailored for Ethernet family, IP family has 8 bits
 * config compiler can know about this and set the field appropriately...
 */

// @returns false on error
// @nexthdr is in host byte order
typedef bool id_from_nexthdr(enum ProtocolID *id, uint16_t nexthdr);

// @returns false on error
// @returns @nexthdr in host byte order
typedef bool nexthdr_from_id(uint16_t *nexthdr, enum ProtocolID id);

// describes one fixed-size protocol header
struct Protocol {
    const char *name;
    const struct ProtocolField *header_fields;
    unsigned header_field_count;
    unsigned bytelength;
    id_from_nexthdr *get_id; // translates the value of the next header field to protocol id
    nexthdr_from_id *get_nexthdr; // translates protocol id to next header field value
};

// the internal id of the protocols is their index in this array
// TODO make this array private?
extern const struct Protocol protocol_list[];
extern const unsigned protocol_count;

// @returns the name of the field type or NULL on unknown type
const char *fieldtype_name_from_type(enum ProtocolFieldType type);

// @returns true if @type is a valid protocol name
bool protocol_type_valid(const char *type);

// @returns PROTO_ID_PAYLOAD for unknown protocol type
// use @protocol_type_valid to make sure that @type is valid
enum ProtocolID protocol_id_from_type(const char *type);

// @returns the type name of the given protocol
// @returns NULL if unknown id
const char *protocol_type_from_id(enum ProtocolID id);

// @returns pointer to the field descriptor
// @returns NULL if the protocol has no field with the given @fieldname
const struct ProtocolField *protocol_get_field_by_name(enum ProtocolID id, const char *fieldname);

// @returns true if @fieldname is valid for this protocol
bool protocol_fieldname_valid(enum ProtocolID id, const char *fieldname);

// @returns pointer to the field descriptor
// @returns NULL if the protocol has no field with the given @type
const struct ProtocolField *protocol_get_field_by_type(enum ProtocolID id, enum ProtocolFieldType type);

// @returns index of the field in the protocol header
// @returns -1 if the protocol has no field with the given @type
int protocol_get_field_idx_by_type(enum ProtocolID id, enum ProtocolFieldType type);

#endif // R2_PROTOCOL_H
