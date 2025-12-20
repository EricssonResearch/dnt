// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam.h"
#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "oam.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h> /* ntohs() */

#include <netinet/in.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

struct OamIfData {
    unsigned port;
    char *oam_ip_str;  // hold IP address in text format
};

const char *oamif_get_ip(const struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    return oid->oam_ip_str;
}

unsigned oamif_get_port(const struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    return oid->port;
}

static bool oam_recv(struct Interface *iface)
{
    char buffer[2000];
    int n;

    n = recv(iface->recvfd, buffer, sizeof(buffer)-1, 0);
    if (n>0) {
        buffer[n]=0;
        oam_receive_outofband(iface, buffer);
    }

    return true;
}

static bool oam_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    log_warning("oam interface %s should not send packet", iface->name);
    return false;
}

static bool oam_open(struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open OAM interface %s: already opened", iface->name);
        return false;
    }

    struct in_addr ip4;
    struct in6_addr ip6;
    int family;

    if (inet_pton(AF_INET, oid->oam_ip_str, &ip4) == 1) {
        family = AF_INET;
    } else if (inet_pton(AF_INET6, oid->oam_ip_str, &ip6) == 1) {
        family = AF_INET6;
    } else {
        log_error("oam interface: invalid ip '%s'", oid->oam_ip_str);
        return false;
    }

    struct sockaddr *sa;
    unsigned sa_len;
    struct sockaddr_in6 addr6;
    struct sockaddr_in addr4;

    if (family == AF_INET6) {
        sa = (struct sockaddr*)&addr6;
        sa_len = sizeof(addr6);
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = ip6;
        addr6.sin6_port = htons(oid->port);
    } else {
        sa = (struct sockaddr*)&addr4;
        sa_len = sizeof(addr4);
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_addr = ip4;
        addr4.sin_port = htons(oid->port);
    }

    int sock = socket(family, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_perror("oam %s cannot create socket", iface->name);
        return false;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        log_perror("oam setsockopt SO_REUSEADDR");
        close(sock);
        return false;
    }

    if (bind(sock, sa, sa_len) < 0) {
        log_perror("oam %s cannot bind socket", iface->name);
        close(sock);
        return false;
    }

    log_info("OAM return interface %s %s port %u", iface->name, oid->oam_ip_str, oid->port);
    iface->recvfd = sock;
    iface->state = IFS_OPEN;
    add_oam_if(iface);
    return true;
}

static bool oam_close(struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    close(iface->recvfd);
    free(oid->oam_ip_str);
    free(oid);
    log_info("OAM interface %s closed", iface->name);
    return true;
}

/*static void oam_port_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct OamIfData *oid = iface->iface_private;
    struct Value val = {&oid->port, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void oam_ip_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct OamIfData *oid = iface->iface_private;
    struct Value val = {&oid->srcip, 0, oid->family == AF_INET6 ? 128 : 32};
    consumer(consumer_state, &val, p);
}

static value_producer *oam_get_property_reader(const struct Interface *iface, const char *property,
                                               enum ProtocolFieldType target_type, const struct Value *target)
{
    struct OamIfData *oid = iface->iface_private;
    if (strcmp(property, "port") == 0) {
        if (target_type != FT_NUMBER) {
            log_error("oam_get_property_reader 'port' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            log_error("oam_get_property_reader 'port' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return oam_port_producer;
    } else if (strcmp(property, "ip") == 0) {
        enum ProtocolFieldType ftype = oid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = oid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            log_error("oam_get_property_reader 'ip' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            log_error("oam_get_property_reader 'ip' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return oam_ip_producer;
    }

    log_error("oam_get_property_reader unknown property '%s'", property);
    return NULL;
}*/

static void oam_print_private_info(const struct Interface *iface, FILE *cmd_w)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    struct in_addr ip4;
    struct in6_addr ip6;

    if (inet_pton(AF_INET, oid->oam_ip_str, &ip4) == 1) {
        fprintf(cmd_w, "    inet \033[35m%s\033[0m port %u\n", oid->oam_ip_str, oid->port);
    } else if (inet_pton(AF_INET6, oid->oam_ip_str, &ip6) == 1) {
        fprintf(cmd_w, "    inet6 \033[34m%s\033[m port %u\n", oid->oam_ip_str, oid->port);
    } else {
        fprintf(cmd_w, "    <invalid address> port %u\n", oid->port);
    }
}

struct Interface *new_oam_interface(const char *name,
                        const char *oam_ip, unsigned port)
{
    _NEW_IFACE(IF_OAM);
    iface->recv = oam_recv;
    iface->send = oam_send;
    iface->open = oam_open;
    iface->close_ = oam_close;
    //iface->get_property_reader = oam_get_property_reader;
    iface->print_private_info = oam_print_private_info;

    struct OamIfData *oid = calloc_struct(OamIfData);
    iface->iface_private = oid;
    oid->oam_ip_str = strdup(oam_ip);
    oid->port = port;
    //note: oid->uid will be set in open()

    return iface;
}
