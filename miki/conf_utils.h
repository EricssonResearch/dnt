
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

#endif // R2_CONF_UTILS_H
