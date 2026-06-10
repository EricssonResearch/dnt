// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_CONF_PACKET_H
#define DNT_CONF_PACKET_H

#include "headerdesc.h"

#include <stdbool.h>

// parse the "*:packet" line for a stream
// @stream name is used in error messages
// modifies the given @line
// @returns NULL on error
struct HeaderDescriptor *parse_packet_line(const char *stream, char *line);

// parse the "*:match" line for a stream
// @stream name is used in error messages
// adds the matches to @headers
// modifies the given @line
// @returns false on error
bool parse_match_line(const char *stream, struct HeaderDescriptor *headers, char *line);

void confheaders_print(const struct HeaderDescriptor *headers);

#endif // DNT_CONF_PACKET_H
