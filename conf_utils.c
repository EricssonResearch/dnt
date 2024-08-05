// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_utils.h"
#include "log.h"
#include "utils.h"
#include "value.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/ether.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

bool foreach_stages(char *line, foreach_callback *cb, void *userdata)
{
    char *l = line;
    while (*l && isspace(*l)) l++; // skip leading whitespace
    if (*l == 0) return true;
    while (1) {
        char *sc = strchr(l, ',');
        if (sc) {
            *sc = 0;
            if (!cb(l, userdata)) return false;
            l = sc + 1;
            while (*l && isspace(*l)) l++; // skip leading whitespace
            if (*l == 0) return true;
        } else {
            if (!cb(l, userdata)) return false;
            return true;
        }
    }
}

bool foreach_tokens(char *stage, foreach_callback *cb, void *userdata)
{
    char *t = stage;
    while (*t && isspace(*t)) t++; // skip leading whitespace
    if (*t == 0) return true;
    while (1) {
        char *sp = t;
        while (*sp && !isspace(*sp)) sp++;
        if (*sp) {
            *sp = 0;
            if (!cb(t, userdata)) return false;
            t = sp + 1;
            while (*t && isspace(*t)) t++; // skip leading whitespace
            if (*t == 0) return true;
        } else {
            if (!cb(t, userdata)) return false;
            return true;
        }
    }
}

bool parse_assignment(char *assign, char **key, char **val)
{
    char *eq = strchr(assign, '=');
    if (eq) {
        char *eq2 = strchr(eq+1, '=');
        if (eq2)
            return false;
        *key = assign;
        *val = eq+1;
        *eq = 0;
        return true;
    } else {
        return false;
    }
}

bool parse_fieldname(char *field, char **headername, char **fieldname)
{
    char *dot = strchr(field, '.');
    if (dot) {
        char *dot2 = strchr(dot+1, '.');
        if (dot2)
            return false;
        *headername = field;
        *fieldname = dot+1;
        *dot = 0;
        return true;
    } else {
        return false;
    }
}

char *header_type_from_name(const char *name)
{
    const char *under = strchr(name, '_');
    if (under) {
        return strndup(name, under-name);
    } else {
        return strdup(name);
    }
}

int read_boolean(const char *string)
{
    if (strcmp(string, "1") == 0 || strcmp(string, "true") == 0 || strcmp(string, "yes") == 0)
        return 1;
    if (strcmp(string, "0") == 0 || strcmp(string, "false") == 0 || strcmp(string, "no") == 0)
        return 0;
    return -1;
}

//TODO move this to utils?
static unsigned highest_set_bit(uint64_t num)
{
    unsigned ret = 0;
    while (num >>= 1) ret++;
    return ret;
}

bool prepare_constant_number(struct Value *val, uint64_t num)
{
    unsigned num_bits = highest_set_bit(num) + 1;
    if (num_bits > val->bitcount) return false;
    //printf("number %u bits: %lu 0x%.8lx\n", num_bits, num, num);

    val->bitoffset %= 8;
    unsigned bits_total = val->bitoffset + val->bitcount;
    unsigned bytes_total = DIVCEIL(bits_total, 8);
    unsigned char *buf = (unsigned char *)calloc(bytes_total, sizeof(unsigned char));
    val->value = buf;
    //printf("val offset %u count %u allocated %u\n", val->bitoffset, val->bitcount, bytes_total);

    // write the bits into the last byte
    unsigned offset = bytes_total*8 - bits_total;
    unsigned idx = bytes_total - 1;
    //printf("last offset %u idx %u\n", offset, idx);
    buf[idx] = (num << offset) & 0xff;

    // write the rest of the bytes if we have more bits
    offset = 8 - offset; // this is also the number of bits written so far
    idx--;
    //printf("offset %u idx %u\n", offset, idx);
    while (offset < num_bits) {
        buf[idx] = (num >> (offset)) & 0xff;
        offset += 8;
        idx--;
        //printf("  offset %u idx %u\n", offset, idx);
    }

    return true;
}

bool read_constant(struct Value *val, enum ProtocolID proto, enum ProtocolFieldType type, const char *string)
{
#define THROW(msg, ...)                                     \
    do {                                                    \
        log_error("read_constant '%s': " msg,               \
                string, ##__VA_ARGS__);                     \
        return false;                                       \
    } while (0)

#define PROCESS_PREFIX(_type, _maxlen)                          \
    char *stringdup = strdup(string);                           \
    char *slash = strchr(stringdup, '/');                       \
    if (slash) {                                                \
        *slash = 0;                                             \
        char *prefixstr = slash+1;                              \
        unsigned prefix;                                        \
        char err;                                               \
        if (sscanf(prefixstr, "%u%c", &prefix, &err) != 1) {    \
            free(stringdup);                                    \
            THROW("invalid " _type " prefix");                  \
        }                                                       \
        if (prefix > _maxlen) {                                 \
            free(stringdup);                                    \
            THROW(_type " prefix too big");                     \
        }                                                       \
        val->bitcount = prefix;                                 \
    }

    val->bitoffset %= 8;

    switch (type) {
        case FT_UNKNOWN:
            THROW("constant cannot be unknown type");
        case FT_NUMBER: {
            uint64_t num;
            if (val->bitcount == 1) {
                int b = read_boolean(string);
                if (b < 0) {
                    THROW("invalid boolean");
                }
                num = b;
            } else if (val->bitcount > 64) {
                THROW("numbers are only supported up to 64 bits, %d is too large", val->bitcount);
            } else {
                char *str_end = NULL;
                num = strtoull(string, &str_end, 0);
                if (str_end == string) {
                    THROW("number is invalid");
                }
                if (num == UINT64_MAX && errno == ERANGE) {
                    THROW("number is too big");
                }
                if (*str_end != 0) {
                    THROW("invalid character '%c' after number", *str_end);
                }
            }
            if (!prepare_constant_number(val, num)) {
                THROW("number doesn't fit into %u bits", val->bitcount);
            }
            return true; }
        case FT_MACADDRESS: {
            if (val->bitoffset != 0 || val->bitcount != 6*8) {
                THROW("bitoffset %u bitcount %u invalid for Ethernet address",
                        val->bitoffset, val->bitcount);
            }
            PROCESS_PREFIX("MAC", 48);
            struct ether_addr *a = ether_aton(stringdup);
            if (a == NULL) {
                free(stringdup);
                THROW("invalid Ethernet address");
            }
            val->value = memdup(a, 6);
            free(stringdup);
            return true; }
        case FT_IPV4ADDRESS: {
            if (val->bitoffset != 0 || val->bitcount != 4*8) {
                THROW("bitoffset %u bitcount %u invalid for IPv4 address",
                        val->bitoffset, val->bitcount);
            }
            PROCESS_PREFIX("IPv4", 32);
            val->value = malloc(4*sizeof(char));
            if (inet_pton(AF_INET, stringdup, val->value) != 1) {
                free(stringdup);
                free(val->value); val->value = NULL;
                THROW("invalid IPv4 address");
            }
            free(stringdup);
            return true; }
        case FT_IPV6ADDRESS: {
            if (val->bitoffset != 0 || val->bitcount != 16*8) {
                THROW("bitoffset %u bitcount %u invalid for IPv6 address",
                        val->bitoffset, val->bitcount);
            }
            PROCESS_PREFIX("IPv6", 128);
            val->value = malloc(16*sizeof(char));
            if (inet_pton(AF_INET6, stringdup, val->value) != 1) {
                free(stringdup);
                free(val->value); val->value = NULL;
                THROW("invalid IPv6 address");
            }
            free(stringdup);
            return true; }
        case FT_TSNSEQ:
            log_warning("It's not a good practice to set sequence number from constant");
            return read_constant(val, proto, FT_NUMBER, string);
        case FT_TSNTSTAMP:
            log_warning("It's not a good practice to set timestamp from constant");
            return read_constant(val, proto, FT_NUMBER, string);
        case FT_NEXTHEADER: {
            if (protocol_type_valid(string)) {
                if (protocol_list[proto].get_nexthdr) {
                    enum ProtocolID val_id = protocol_id_from_type(string);
                    uint16_t nexthdr;
                    if (protocol_list[proto].get_nexthdr(&nexthdr, val_id)) {
                        if (prepare_constant_number(val, nexthdr)) {
                            return true;
                        } else {
                            // if this happens then get_nexthdr() of the protocol is bugged
                            THROW("invalid nexthdr value 0x%x for protocol %s",
                                    nexthdr, protocol_type_from_id(proto));
                        }
                    } else {
                        THROW("invalid nexthdr type '%s' for protocol %s",
                                string, protocol_type_from_id(proto));
                    }
                } else {
                    THROW("protocol %s doesn't have nexthdr field",
                            protocol_type_from_id(proto));
                }
            } else {
                return read_constant(val, proto, FT_NUMBER, string);
            }
        }
        case FT_TTL:
        case FT_CHECKSUM:
            return read_constant(val, proto, FT_NUMBER, string);
    }
    return false;
#undef THROW
}

