// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_CONF_UTILS_H
#define DNT_CONF_UTILS_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

struct Value;

typedef bool foreach_callback(char *str, void *userdata);

// split the @line into stages by ',' and call @cb for all of them
// modifies the given @line
// removes starting whitespace, keeps trailing whitespace
// stops and returns false if @cb returns false
// empty line is not an error
// @returns true on success
bool foreach_stages(char *line, foreach_callback *cb, void *userdata);

// split the @stage into tokens by whitespace, call @cb for all of them
// modifies the given @stage
// removes starting and trailing whitespace
// stops and returns false if @cb returns false
// empty stage is not an error
// @returns true on success
bool foreach_tokens(char *stage, foreach_callback *cb, void *userdata);

// interprets @assign as an assignment in the form of "key=val"
// sets @key and @val to the beginning of the key and the value
// modifies the given @assign
// @returns false if @assign has invalid format
bool parse_assignment(char *assign, char **key, char **val);

// interprets @field as a field name in the form of "headername.fieldname"
// sets @headername and fieldname to the beginning of the names
// modifies the given @field
// @returns false if @field has invalid format
bool parse_fieldname(char *field, char **headername, char **fieldname);

// header name is in the form of "type[_identifier]", this extracts the type
// @returns a new string (even if name has no identifier part)
char *header_type_from_name(const char *name);

// interprets the given @string as boolean
// true values: 1, true, yes
// false values: 0, false, no
// @returns -1 if not a valid value
int read_boolean(const char *string);

// puts the number @num into the value @val
// the target space is specified by val->bitoffset and val->bitcount
// allocates buffer in val->value for the result
// the result is in network byte order
// @returns false if the number doesn't fit into the space
bool prepare_constant_number(struct Value *val, uint64_t num);

// reads a constant from @string and stores it such that it's easy to write into a packet
// interprets @string as a value of @type
// @proto is the protocol type of the header @val will be written into
// uses @bitoffset and @bitcount of @val to position the result
// if the result is shorter than @bitcount, it will be padded
//   with 0 bits on the left
// allocates new buffer in @val->value for the result
// @returns false if the conversion cannot be done
//      @string is not valid for @type
//      the number doesn't fit @bitcount
bool read_constant(struct Value *val, enum ProtocolID proto, enum ProtocolFieldType type, const char *string);

#endif // DNT_CONF_UTILS_H
