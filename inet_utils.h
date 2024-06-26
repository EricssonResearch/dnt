// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_INET_UTILS_H
#define R2_INET_UTILS_H

#include <stdbool.h>

// parses @str, accepts 'ipv4' 'ipv4:port', 'ipv6', '[ipv6]', '[ipv6]:port'
// see RFC 2732
// only accepts valid address strings and ports
// allocates a new string for @ip
// only sets @port if @str specifies it
// @returns true on success, otherwise doesn't touch the output parameter
bool parse_ip_port(const char *str, char **ip, unsigned *port);

#endif // R2_INET_UTILS_H
