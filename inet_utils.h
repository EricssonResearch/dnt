// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_INET_UTILS_H
#define R2_INET_UTILS_H

#include <stdbool.h>

// parses @str, accepts 'ipv4' 'ipv4:port', 'ipv6', '[ipv6]', '[ipv6]:port'
// see RFC 2732
// only accepts valid address strings and ports
// allocates a new string for @ip
// port is always decimal, and returned in host byte order
// only sets @port if @str specifies it
// @returns true on success, otherwise doesn't touch the output parameters
bool parse_ip_port(const char *str, char **ip, unsigned *port);

// parses @src, accepts MAC addresses in the colon-separated hex form
// writes the result into @dst buffer, which should have enough space (6 bytes)
// @returns 1 on success, 0 on failure
int ether_pton(const char *src, void *dst);

// parses @str, accepts 'mac' 'mac+decvlan' 'mac+0xhexvlan' 'mac+0octvlan'
// only accepts valid address strings and vlan numbers
// vlan is interpreted as a 16 bit number that includes PCP and DEI
// vlan is returned in host byte order
// allocates a new string for @mac
// only sets @vlan if @str specifies it
// @returns true on success, otherwise doesn't touch the output parameters
bool parse_mac_vlan(const char *str, char **mac, unsigned *vlan);

#endif // R2_INET_UTILS_H
