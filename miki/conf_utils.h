
#ifndef R2_CONF_UTILS_H
#define R2_CONF_UTILS_H

#include <stdbool.h>

typedef bool foreach_callback(char *str, void *userdata);

// split the line into stages by ';', call @cb for all of them
// modifies the given @line
// stops if @cb returns false
void foreach_stages(char *line, foreach_callback *cb, void *userdata);

// split the stage into tokens by whitespace, call @cb for all of them
// modifies the given @stage
// stops if @cb returns false
void foreach_tokens(char *stage, foreach_callback *cb, void *userdata);

// interprets @assign as an assignment in the form of "key=val"
// sets @key and @val to the beginning of the key and the value
// modifies the given @assign
// @returns false if @assign is not an assignment
bool parse_assignment(char *assign, char **key, char **val);

// interprets the given string as boolean
// true values: 1, true, yes
// false values: 0, false, no
// @returns -1 if not a valid value
// TODO case-insensitive?
int read_boolean(const char *val);

#endif // R2_CONF_UTILS_H
