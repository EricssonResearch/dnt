// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_udp_out.h"
#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "packet.h"
#include "parsetree.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <netdb.h> /* getaddrinfo() */

DEFAULT_LOGGING_MODULE(IFUDPOUT, WARNING)

struct UdpOutIfData {
    int sock;
    int ifindex;
    int mtu;
    unsigned sport;
    unsigned dport;
    int family;
    unsigned priority;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } dstip;
    void *errq_monitor;
};

static struct Packet *udpout_recv(struct Interface *iface)
{
    log_warning("udp-out interface %s recv how??\n", iface->name);
    return NULL;
}

static bool udpout_send(struct Interface *iface, struct Packet *p)
{
    struct UdpOutIfData *uid = iface->iface_private;
    // our socket is connected, so no dst
    return iface_common_send(iface, p, uid->sock, NULL, 0);
}

static bool udpout_open(struct Interface *iface)
{
    struct UdpOutIfData *uid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open udp-out interface %s: already opened\n", iface->name);
        return false;
    }
    int sock = uid->sock;

    struct ifreq if_mtu, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, iface->ifname, IFNAMSIZ-1);
    strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
    if (ioctl(sock, SIOCGIFMTU, &if_mtu) < 0) {
        log_perror("udp-out SIOCGIFMTU");
        return false;
    }
    if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
        log_perror("udp-out SIOCGIFINDEX");
        return false;
    }
    uid->mtu = if_mtu.ifr_mtu;
    uid->ifindex = if_idx.ifr_ifindex;

    if (uid->family == AF_INET6) {
        int tos = (uid->priority & 7) << 5;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(int)) < 0) {
            log_perror("udp-out setsockopt IPV6_TCLASS");
            return false;
        }
        // it seems that setting IPv6 MTU discovery mode is also needed here
        int val = IPV6_PMTUDISC_PROBE;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &val, sizeof(val)) < 0) {
            log_perror("udp-out setsockopt IP_MTU_DISCOVER");
            return false;
        }
    } else {
        int tos = (uid->priority & 7) << 5;
        if (setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0) {
            log_perror("udp-out setsockopt IP_TOS");
            return false;
        }
        // it seems that setting IPv4 MTU discovery mode is also needed here
        int val = IP_PMTUDISC_PROBE;
        if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
            log_perror("udp-out setsockopt IP_MTU_DISCOVER");
            return false;
        }
    }
    if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &uid->priority, sizeof(int)) < 0) {
        log_perror("udp-out setsockopt SO_PRIORITY");
        return false;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        log_perror("udp-out setsockopt SO_BINDTODEVICE");
        return false;
    }
/*
     // SO_DONTROUTE option also solves MTU problem. However, it will only work on directly
     // connected interfaces. So we are not using this option, instead we use IP MTU discovery
     // probe mode.
     int optval = 1;
     int optlen = sizeof(optval);
     if (setsockopt (sock, SOL_SOCKET, SO_DONTROUTE, (void*) &optval, optlen) == -1)
        log_perror("udp-out setsockopt SO_DONTROUTE");
*/

    uid->errq_monitor = monitor_error_queue(sock, uid->family, iface->name);

    iface->dropstat_cntr = 0;
    iface->dropstat_last_warn = 0;
    iface->state = IFS_OPEN;
    return true;
}

static bool udpout_close(struct Interface *iface)
{
    struct UdpOutIfData *uid = iface->iface_private;
    stop_monitoring_error_queue(uid->errq_monitor);
    close(uid->sock);
    free(uid);
    return true;
}

static void udpout_srcport_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct UdpOutIfData *uid = iface->iface_private;
    struct Value val = {&uid->sport, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void udpout_dstport_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct UdpOutIfData *uid = iface->iface_private;
    struct Value val = {&uid->dport, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void udpout_dstip_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct UdpOutIfData *uid = iface->iface_private;
    struct Value val = {&uid->dstip, 0, uid->family == AF_INET6 ? 128 : 32};
    consumer(consumer_state, &val, p);
}

static value_producer *udpout_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    struct UdpOutIfData *uid = iface->iface_private;

    if (strcmp(property, "srcport") == 0) {
        if (target_type != FT_NUMBER) {
            log_error("udpout_get_property_reader 'srcport' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            log_error("udpout_get_property_reader 'srcport' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_srcport_producer;
    } else if (strcmp(property, "dstport") == 0) {
        if (target_type != FT_NUMBER) {
            log_error("udpout_get_property_reader 'dstport' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            log_error("udpout_get_property_reader 'dstport' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_dstport_producer;
    } else if (strcmp(property, "dstip") == 0) {
        enum ProtocolFieldType ftype = uid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = uid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            log_error("udpout_get_property_reader 'srcip' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            log_error("udpout_get_property_reader 'srcip' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_dstip_producer;
    }

    log_error("udpout_get_property_reader unknown property '%s'\n", property);
    return NULL;
}

struct Interface *new_udp_out_interface(const char *name, const char *ifname,
        unsigned src_port, const char *dst_ip, unsigned dst_port, unsigned priority)
{
    struct Interface *iface = calloc_struct(Interface);
    iface->name = strdup(name);
    iface->ifname = strdup(ifname);
    iface->type = IF_UDP_OUT;
    iface->recv = udpout_recv;
    iface->send = udpout_send;
    iface->open = udpout_open;
    iface->close_ = udpout_close;
    iface->get_property_reader = udpout_get_property_reader;

    //TODO can we defer creating the socket to udpout_open?
    //TODO if remote is mobile we don't have a dstip yet!
    //      who sets it later? OAM?
    //TODO if an action wants to read our dstip property we need to know at least the version!
    //      this is checked between init and open!

    char port_str[15];
    sprintf(port_str, "%u", dst_port);
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0; //TODO AI_NUMERICHOST?
    int err = getaddrinfo(dst_ip, port_str, &hints, &result);

    if (err) {
        log_error("udp-out invalid dstip '%s'\n", dst_ip);
        return false;
    }

    int sock = -1;
    for (rp=result; rp!=NULL; rp=rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname)) < 0) {
            log_perror("udp-out setsockopt SO_BINDTODEVICE");
            close(sock);
            sock = -1;
            continue;
        }

        if (src_port) {
            if (rp->ai_family == AF_INET6) {
                struct sockaddr_in6 addr6;
                memset(&addr6, 0, sizeof(addr6));
                addr6.sin6_family = AF_INET6;
                addr6.sin6_addr = in6addr_any;
                addr6.sin6_port = htons(src_port);
                if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
                    log_perror("udp-out bind sock udp6");
                    close(sock);
                    sock = -1;
                    continue;
                }
                /* set IPv6 MTU discovery mode before connecting */
                int val = IPV6_PMTUDISC_PROBE;
                if (setsockopt(sock, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &val, sizeof(val)) < 0) {
                    log_perror("udp-out setsockopt IP_MTU_DISCOVER");
                    return false;
                }
            } else {
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_ANY);
                addr.sin_port = htons(src_port);
                if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    log_perror("udp-out bind sock udp4");
                    close(sock);
                    sock = -1;
                    continue;
                }
                /* set IPv4 MTU discovery mode before connecting */
                int val = IP_PMTUDISC_PROBE;
                if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
                    log_perror("udp-out setsockopt IP_MTU_DISCOVER");
                    return false;
                }
            }
        }

    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        log_perror("udp-out connect");
        close(sock);
        sock = -1;
    }

    if (sock < 0) {
        log_error("udp-out can't make socket with dstip '%s'\n", dst_ip);
        freeaddrinfo(result);
        return false;
    }

    iface->state = IFS_INIT;

    struct UdpOutIfData *uid = calloc_struct(UdpOutIfData);
    iface->iface_private = uid;
    uid->sock = sock;
    uid->sport = src_port;
    uid->dport = dst_port;
    uid->family = rp->ai_family;
    if (uid->family == AF_INET6) {
        uid->dstip.v6 = ((struct sockaddr_in6*)(rp->ai_addr))->sin6_addr;
    } else {
        uid->dstip.v4 = ((struct sockaddr_in*)(rp->ai_addr))->sin_addr;
    }
    uid->priority = priority;
    freeaddrinfo(result);

    iface->parsetree = new_parsetree(iface);
    parsetree_ref(iface->parsetree);

    return iface;
}
