// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "oam.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "if_utils.h"
#include "interface.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>

#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>


//void *get_in_addr(struct sockaddr *sa);

struct OamIfData {
    //int oam_cmd_fd;
    unsigned port;
    int family;
    char *oam_ip_str;  // hold IP address in text format
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } srcip;
};

const char *oam_get_oam_ip(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->oam_ip_str;
}

unsigned oam_get_oam_port(const struct Interface *iface)
{
    struct OamIfData *oid = iface->iface_private;
    return oid->port;
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

    int sock = socket(oid->family, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("oam socket");
        return false;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("oam setsockopt SO_REUSEADDR");
        close(sock);
        return false;
    }

/*  ... if we want to verify that interface has the right IP, this will be needed
    struct ifaddrs *ifaddr;
    if (iface->ifname != 0) {
        struct ifreq  if_idx;
        memset(&if_idx, 0, sizeof(struct ifreq));
        strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
        if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
            perror("oam SIOCGIFINDEX");
            close(sock);
            return false;
        }
        //      oid->ifindex = if_idx.ifr_ifindex;
    }

    if (getifaddrs(&ifaddr) < 0) {
        perror("oam getifaddrs");
        close(sock);
        return false;
    }

    bool srcip_set = false;
    for (struct ifaddrs *ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != oid->family) continue;
        if ( (iface->ifname != NULL) && strcmp(ifa->ifa_name, iface->ifname) != 0) continue;

        //print_ifaddrs(ifa);

        if (family == AF_INET6) {
            struct in6_addr *a6 = &((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
            if (IN6_IS_ADDR_LINKLOCAL(a6)) continue;
            oid->srcip.v6 = *a6;
            srcip_set = true;
            break;
        } else if (family == AF_INET) {
            oid->srcip.v4 = ((struct sockaddr_in*)(ifa->ifa_addr))->sin_addr;
            srcip_set = true;
            break;
        }
    }
    freeifaddrs(ifaddr);
    if (!srcip_set) {
        fprintf(stderr, "open oam interface %s: no address on interface %s\n", iface->name, iface->ifname);
        close(sock);
        return false;
    }
*/
    if (oid->family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(oid->port);
        if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
            perror("oam bind sock6");
            return false;
        }
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(oid->port);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("oam bind sock");
            return false;
        }
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
    free(oid);
    return true;
}

static void oam_port_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
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
}

bool init_oam_interface(struct Interface *iface, const char *name,
                        const char *oam_ip, unsigned port, unsigned ipversion)
{
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    iface->type = IF_OAM;
    iface->state = IFS_INIT;
    iface->recv = oam_recv;
    iface->send = oam_send;
    iface->open = oam_open;
    iface->close_ = oam_close;
    iface->get_property_reader = oam_get_property_reader;

    struct OamIfData *oid = calloc_struct(OamIfData);
    iface->iface_private = oid;
    oid->oam_ip_str = strdup(oam_ip);
    oid->port = port;
    //TODO derive family from ip string with getaddrinfo()
    oid->family = ipversion == 6 ? AF_INET6 : AF_INET;

    return true;
}
