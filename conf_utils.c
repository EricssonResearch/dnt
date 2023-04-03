
#include "conf_utils.h"
#include "transfer.h"
#include "utils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <arpa/inet.h>
#include <netinet/ether.h>

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
    char *under = strchr(name, '_');
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
    unsigned char *buf = calloc(bytes_total, sizeof(unsigned char));
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

bool read_constant(struct Value *val, enum ProtocolFieldType type, const char *string)
{
    val->bitoffset %= 8;

    switch (type) {
        case FT_UNKNOWN:
            fprintf(stderr, "constant cannot be unknown type\n");
            return false;
        case FT_NUMBER: {
            uint64_t num;
            if (val->bitcount == 1) {
                int b = read_boolean(string);
                if (b < 0) {
                    fprintf(stderr, "invalid boolean '%s'\n", string);
                    return false;
                }
                num = b;
            } else {
                char c;
                if (sscanf(string, "%li%c", &num, &c) != 1) {
                    fprintf(stderr, "invalid number '%s'\n", string);
                    return false;
                }
            }
            if (!prepare_constant_number(val, num)) {
                fprintf(stderr, "number '%s' doesn't fit into %u bits\n", string, val->bitcount);
                return false;
            }
            return true; }
        case FT_MACADDRESS:
            if (val->bitoffset != 0 || val->bitcount != 6*8) {
                fprintf(stderr, "bitoffset %u bitcount %u invalid for Ethernet address\n",
                        val->bitoffset, val->bitcount);
                return false;
            }
            struct ether_addr *a = ether_aton(string);
            if (a == NULL) {
                fprintf(stderr, "invalid Ethernet address '%s'\n", string);
                return false;
            }
            val->value = malloc(6*sizeof(char));
            memcpy(val->value, a, 6);
            return true;
        case FT_IPV4ADDRESS:
            if (val->bitoffset != 0 || val->bitcount != 4*8) {
                fprintf(stderr, "bitoffset %u bitcount %u invalid for IPv4 address\n",
                        val->bitoffset, val->bitcount);
                return false;
            }
            val->value = malloc(4*sizeof(char));
            if (inet_pton(AF_INET, string, val->value) != 1) {
                fprintf(stderr, "invalid IPv4 address '%s'\n", string);
                return false;
            }
            return true;
        case FT_IPV6ADDRESS:
            if (val->bitoffset != 0 || val->bitcount != 16*8) {
                fprintf(stderr, "bitoffset %u bitcount %u invalid for IPv6 address\n",
                        val->bitoffset, val->bitcount);
                return false;
            }
            val->value = malloc(16*sizeof(char));
            if (inet_pton(AF_INET6, string, val->value) != 1) {
                fprintf(stderr, "invalid IPv6 address '%s'\n", string);
                return false;
            }
            return true;
        case FT_TSNSEQ:
            fprintf(stderr, "warning: it's not a good practice to set sequence number from constant\n");
            return read_constant(val, FT_NUMBER, string);
        case FT_TSNTSTAMP:
            fprintf(stderr, "warning: it's not a good practice to set timestamp from constant\n");
            return read_constant(val, FT_NUMBER, string);
    }
    return false;
}

