// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam.h"
#include "interface.h"
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


struct OamIfData {
    unsigned port;
    char *oam_ip_str;  // hold IP address in text format
    unsigned short uid; // unique id of this iface
};

const char *oam_get_ip(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->oam_ip_str;
}

unsigned oam_get_port(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->port;
}

unsigned short oam_get_uid(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->uid;
}

static struct Packet *oam_recv(struct Interface *iface)
{
    char buffer[512];
    int n;

    n = recv(iface->recvfd, buffer, sizeof(buffer)-1, 0);
    if (n>0) {
        buffer[n]=0;
        oam_recv_reply(buffer);
    }

    return NULL;
}

static bool oam_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    fprintf(stderr, "oam interface %s should not send packet\n", iface->name);
    return false;
}

static bool oam_open(struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        fprintf(stderr, "open OAM interface %s: already opened\n", iface->name);
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
        fprintf(stderr, "oam interface: invalid ip '%s' : %s\n", oid->oam_ip_str, port_str);
        return false;
    }

    int sock;
    int enable = 1;
    for (rp=result; rp!=NULL; rp=rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0)
            continue;

        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
            perror("oam setsockopt SO_REUSEADDR");
            close(sock);
            return false;
        }

        if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sock);
    }
    int family = rp->ai_family;
    freeaddrinfo(result);
    if (rp == NULL) {
        fprintf(stderr, "oam interface: could not bind to ip '%s' : %s\n", oid->oam_ip_str, port_str);
        return false;
    }

    if (family == AF_INET6) {
        struct sockaddr_in6 *i6 = (struct sockaddr_in6 *)(rp->ai_addr);
        oid->uid = ntohs(i6->sin6_addr.s6_addr16[7]);
        printf("oam if ip6 '%s' uid %u\n", oid->oam_ip_str, oid->uid);
    } else {
        struct sockaddr_in *i4 = (struct sockaddr_in *)(rp->ai_addr);
        oid->uid = ntohl(i4->sin_addr.s_addr) & 0xffff;
        printf("oam if ip4 '%s' uid %u\n", oid->oam_ip_str, oid->uid);
    }

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
            fprintf(stderr, "oam_get_property_reader 'port' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            fprintf(stderr, "oam_get_property_reader 'port' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return oam_port_producer;
    } else if (strcmp(property, "ip") == 0) {
        enum ProtocolFieldType ftype = oid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = oid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            fprintf(stderr, "oam_get_property_reader 'ip' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            fprintf(stderr, "oam_get_property_reader 'ip' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return oam_ip_producer;
    }

    fprintf(stderr, "oam_get_property_reader unknown property '%s'\n", property);
    return NULL;
}*/

bool init_oam_interface(struct Interface *iface, const char *name,
                        const char *oam_ip, unsigned port)
{
    bzero(iface, sizeof(*iface));
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

    return true;
}
