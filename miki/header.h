
#ifndef R2_HEADER_H
#define R2_HEADER_H

struct Packet;

//TODO header index or start pointer?
//  index:
//      constant for all packets of the stream
//      we heed the whole Packet to read the field (Action has it...)
//  pointer:
//      we have to adjust it to the current packet
//      we can't have HeaderField* in Action constructors
struct HeaderField {
    unsigned header_idx;
    unsigned bitoffset;
    unsigned bitcount;
};

// the value should be stored in network byte order
struct HeaderValue {
    unsigned bitcount;
    void *value;
};

typedef void field_assign(struct Packet *p, struct HeaderField *field, struct HeaderValue *value);

struct HeaderFieldAssign {
    field_assign *assign;
    struct HeaderField field;
    struct HeaderValue value;
    const char *text;
};

/* TODO
 * assigning constant value
 *      the source is pointed by HeaderValue::value
 *      the destination is a place pointed by HeaderField
 *      @HeaderFieldAssign::assign is the function that writes the value
 *      we have a series of such functions depending on the bitcount and offset
 *      the config compiler chooses a suitable one
 *      the compiler will also validate value->bitcount against field->bitcount
 * assigning dynamic value (e.g. sequence generator)
 *      HeaderFieldAssign::assign is the generator's entry point
 *      it creates the value in a local variable
 *      it writes the header by calling an assignment function
 *      TODO how does it have an assignment function?
 *          config compiler gives that when creating the object
 */

// returns a suitble function for writing to @field
//TODO the config compiler will use this
field_assign *get_assign_function(const struct HeaderField *field);

#endif // R2_HEADER_H
