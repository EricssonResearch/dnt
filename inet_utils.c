// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "inet_utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <arpa/inet.h>

bool parse_ip_port(const char *str, char **ip, unsigned *port)
{
    if (str[0] == '[') {
            int n=1;
            while (str[n]) {
                if (str[n] == ']') break;
                n++;
            }
            if (str[n] == ']') {
                char *ip6_s = strndup(str+1, n-1);
                struct in6_addr ip6;
                if (inet_pton(AF_INET6, ip6_s, &ip6) != 1) {
                    free(ip6_s);
                    return false;
                }
                n++;
                if (str[n]) {
                    unsigned p;
                    char err;
                    if (sscanf(str+n, ":%u%c", &p, &err) != 1) {
                        free(ip6_s);
                        return false;
                    }
                    if (p > 0xffff) {
                        free(ip6_s);
                        return false;
                    }
                    *port = p;
                }
                *ip = ip6_s;
                return true;
            } else {
                // missing ']'
                return false;
            }
    } else {
        struct in6_addr ip6;
        if (inet_pton(AF_INET6, str, &ip6) == 1) {
            // IPv6 without port
            *ip = strdup(str);
            return true;
        } else {
            const char *colon = strchr(str, ':');
            if (colon) {
                const char *colon2 = strchr(colon+1, ':');
                if (colon2) {
                    return false;
                }
                char *ip_s = strndup(str, colon-str);
                struct in_addr ip4;
                if (inet_pton(AF_INET, ip_s, &ip4) != 1) {
                    free(ip_s);
                    return false;
                }
                unsigned p;
                char err;
                if (sscanf(colon, ":%u%c", &p, &err) != 1) {
                    free(ip_s);
                    return false;
                }
                if (p > 0xffff) {
                    free(ip_s);
                    return false;
                }
                *ip = ip_s;
                *port = p;
                return true;
            } else {
                // no port
                char *ip_s = strdup(str);
                struct in_addr ip4;
                if (inet_pton(AF_INET, ip_s, &ip4) != 1) {
                    free(ip_s);
                    return false;
                }
                *ip = ip_s;
                return true;
            }
        }
        return false;
    }
}

int ether_pton(const char *src, void *dst)
{
    unsigned char buf[6];
    char err;
    if (sscanf(src, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx%c",
                buf+0, buf+1, buf+2, buf+3, buf+4, buf+5, &err) != 6) {
        return 0;
    }
    memcpy(dst, buf, 6);
    return 1;
}

const char *ether_ntop(const void *src, char *dst, unsigned dstsize)
{
    const unsigned char *src_char = (const unsigned char *)src;
    if (dstsize < ETHER_ADDSTRLEN)
        return NULL;
    snprintf(dst, dstsize, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
            src_char[0], src_char[1], src_char[2],
            src_char[3], src_char[4], src_char[5]);
    return dst;
}

bool parse_mac_vlan(const char *str, char **mac, unsigned *vlan)
{
    const char *plus = strchr(str, '+');
    if (plus) {
        // mac+vlan
        char *tmpmac = strndup(str, plus-str);
        unsigned char dstmac[6];
        if (ether_pton(tmpmac, dstmac)) {
            // mac okay, parse vlan
            unsigned vl;
            char *end;
            vl = strtoul(plus+1, &end, 0);
            if (end == plus+1) {
                // invalid number
                free(tmpmac);
                return false;
            } else {
                *mac = tmpmac;
                *vlan = vl;
                return true;
            }
        } else {
            free(tmpmac);
            return false;
        }
    } else {
        // only mac
        unsigned char dstmac[6];
        if (ether_pton(str, dstmac)) {
            *mac = strdup(str);
            *vlan = 0;
            return true;
        } else {
            return false;
        }
    }
}
