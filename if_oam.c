// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam.h"
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

#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

struct OamIfData {
    unsigned port;
    char *oam_ip_str;  // hold IP address in text format
    unsigned short uid; // unique id of this iface
};

const char *oamif_get_ip(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->oam_ip_str;
}

unsigned oamif_get_port(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->port;
}

unsigned short oamif_get_uid(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->uid;
}

static bool oam_recv(struct Interface *iface)
{
    char buffer[2000];
    int n;

    n = recv(iface->recvfd, buffer, sizeof(buffer)-1, 0);
    if (n>0) {
        buffer[n]=0;
        oam_recv_reply(buffer);
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
    struct OamIfData *oid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open OAM interface %s: already opened", iface->name);
        return false;
    }

    char port_str[15];
    sprintf(port_str, "%u", oid->port);
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0; //TODO AI_NUMERICHOST?
    int err = getaddrinfo(oid->oam_ip_str, port_str, &hints, &result);
    if (err) {
        log_error("oam interface: invalid ip '%s' : %s", oid->oam_ip_str, port_str);
        return false;
    }

    int sock;
    int enable = 1;
    for (rp=result; rp!=NULL; rp=rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0)
            continue;

        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
            log_perror("oam setsockopt SO_REUSEADDR");
            close(sock);
            return false;
        }

        if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sock);
    }
    if (rp == NULL) {
        log_error("oam interface: could not bind to ip '%s' : %s", oid->oam_ip_str, port_str);
        freeaddrinfo(result);
        return false;
    }

    if (rp->ai_family == AF_INET6) {
        struct sockaddr_in6 *i6 = (struct sockaddr_in6 *)(rp->ai_addr);
        oid->uid = ntohs(i6->sin6_addr.s6_addr16[7]);
        log_info("OAM interface IPv6 '%s' port %s uid 0x%.4x", oid->oam_ip_str, port_str, oid->uid);
    } else {
        struct sockaddr_in *i4 = (struct sockaddr_in *)(rp->ai_addr);
        oid->uid = ntohl(i4->sin_addr.s_addr) & 0xffff;
        log_info("OAM interface IPv4 '%s' port %s uid 0x%.4x", oid->oam_ip_str, port_str, oid->uid);
    }
    freeaddrinfo(result);

    iface->recvfd = sock;
    iface->state = IFS_OPEN;
    add_oam_if(iface);
    return true;
}

static bool oam_close(struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    close(iface->recvfd);
    free(oid->oam_ip_str);
    free(oid);
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

struct Interface *new_oam_interface(const char *name,
                        const char *oam_ip, unsigned port)
{
    struct Interface *iface = calloc_struct(Interface);
    iface->name = strdup(name);
    iface->type = IF_OAM;
    iface->state = IFS_INIT;
    iface->recv = oam_recv;
    iface->send = oam_send;
    iface->open = oam_open;
    iface->close_ = oam_close;
    //iface->get_property_reader = oam_get_property_reader;

    struct OamIfData *oid = calloc_struct(OamIfData);
    iface->iface_private = oid;
    oid->oam_ip_str = strdup(oam_ip);
    oid->port = port;
    //note: oid->uid will be set in open()

    return iface;
}
