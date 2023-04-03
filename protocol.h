
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
// @nexthdr is in network byte order
typedef bool id_from_nexthdr(int *id, uint16_t nexthdr);

// @returns false on error
// @returns @nexthdr in network byte order
typedef bool nexthdr_from_id(uint16_t *nexthdr, int id);

// describes one fixed-size protocol header
struct Protocol {
    const char *name;
    const struct ProtocolField *header_fields;
    unsigned header_field_count;
    unsigned bytelength;
    unsigned nexthdr_idx; // index of the next header field
    id_from_nexthdr *get_id; // translates the value of the next header field to protocol id
    nexthdr_from_id *get_nexthdr; // translates protocol id to next header field value
};

// the internal id of the protocols is their index in this array
// TODO make this array private?
extern const struct Protocol protocol_list[];
extern unsigned protocol_count;

//TODO autogenerate this list (and turn it into an enum)
#define PROTO_ID_PAYLOAD 0
#define PROTO_ID_ETH 1
#define PROTO_ID_SVLAN 2
#define PROTO_ID_CVLAN 3
#define PROTO_ID_RTAG 4
#define PROTO_ID_TTAG 5
#define PROTO_ID_MPLS 6

// @returns the name of the field type or NULL on unknown type
const char *fieldtype_name_from_type(enum ProtocolFieldType type);

// @returns -1 if unknown protocol type
//TODO const struct Protocol *protocol_from_type()
int protocol_id_from_type(const char *type);

// @returns the type name of the given protocol
// @returns NULL if unknown id
// TODO enum ProtocolID id
const char *protocol_type_from_id(int id);

// @returns pointer to the field descriptor with @fieldname
// @returns NULL if the protocol has no field with the given name
const struct ProtocolField *protocol_get_field_by_name(int id, const char *fieldname);

// @returns true if @fieldname is valid for this protocol
bool protocol_fieldname_valid(int id, const char *fieldname);

#endif // R2_PROTOCOL_H
