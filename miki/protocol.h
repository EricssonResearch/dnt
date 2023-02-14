
#ifndef R2_PROTOCOL_H
#define R2_PROTOCOL_H

#include <stdbool.h>

struct ProtocolField {
    const char *name;
    unsigned bitoffset;
    unsigned bitcount;
};

//TODO parameter?
//  number: we don't need a header to call this function
//  pointer to the header: this function can get the nexthdr value
typedef int id_from_nexthdr(void);

//TODO this just returns a number? how will we set the header field?
typedef unsigned nexthdr_from_id(int id);

struct Protocol {
    const char *name;
    struct ProtocolField *header_fields;
    unsigned header_field_count;
    //TODO functions for handling the nextheader-type fields
    //      we need 2way translate: internalid->nexthdr, nexthdr->internalid

};


// the internal id of the protocols is their index in this array
extern struct Protocol protocol_list[];
extern unsigned protocol_count;

// @returns -1 if unknown protocol type
int protocol_id_from_type(const char *type);

// @returns NULL if unknown id
const char *protocol_name_from_id(int id);

// @returns true if @fieldname is valid for this protocol
bool protocol_fieldname_valid(int id, const char *fieldname);

#endif // R2_PROTOCOL_H
