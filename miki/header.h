
#ifndef R2_HEADER_H
#define R2_HEADER_H

struct Packet;

struct HeaderField {
    unsigned header_idx;
    unsigned bitoffset;
    unsigned bitcount;
};

// the value must be stored in network byte order
struct HeaderValue {
    void *value;
    unsigned bitcount;
};

typedef void field_assign(struct Packet *p, struct HeaderField *target, struct HeaderValue *source);

typedef void value_generator(struct Packet *p, struct HeaderField *target, field_assign *assign, void *state);

// the Edit action has an array of these
struct HeaderFieldAssign {
    field_assign *assign;
    struct HeaderField target;
    value_generator *generator;
    void *generator_state;
    struct HeaderValue source;
    const char *text;
};

// returns a suitable function for writing to @field
// the @header_idx is ignored
//TODO the config compiler will use this
field_assign *get_assign_function(const struct HeaderField *field);

// returns a suitable function for reading this @field
// the @header_idx is ignored
// the state parameter of the reader function is struct HeaderField *source
//TODO the config compiler will use this
//TODO how can the header matching use the returned function?
value_generator *get_read_function(const struct HeaderField *field);

#endif // R2_HEADER_H
