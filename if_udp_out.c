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
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

struct UdpOutIfData {
    int sock;
    pthread_mutex_t mutex;
    int ifindex;
    int mtu;
    char *ifname;
    char *dst_ip;
    unsigned sport;
    unsigned dport;
    int family;
    unsigned priority;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } dstip;
    struct MonitorState *errq_monitor;
    bool opened;
};

static bool udpout_recv(struct Interface *iface)
{
    log_error("udp-out interface %s recv how??", iface->name);
    return false;
}

static bool udpout_send(struct Interface *iface, struct Packet *p)
{
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;
    pthread_mutex_lock(&uid->mutex);
    if (iface->state == IFS_OPEN) {
        // our socket is connected, so no dst
        bool ret = iface_common_send(iface, p, uid->sock, NULL, 0);
        pthread_mutex_unlock(&uid->mutex);
        return ret;
    } else {
        // we don't know the dst address yet
        pthread_mutex_unlock(&uid->mutex);
        return false;
    }
}

static bool udpout_open(struct Interface *iface)
{
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open udp-out interface %s: already opened", iface->name);
        return false;
    }

    uid->opened = true;

    if (strcmp(uid->dst_ip, "<none>") == 0) {
        log_info("open udp-out interface %s: deferring open until we learn the dst ip v%u",
                iface->name, uid->family == AF_INET6 ? 6 : 4);
        return true;
    }

    int sock = socket(uid->family, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_perror("udp-out %s cannot create socket", iface->name);
            return false;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, uid->ifname, strlen(uid->ifname)) < 0) {
        log_perror("udp-out setsockopt SO_BINDTODEVICE");
        close(sock);
        return false;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &uid->priority, sizeof(int)) < 0) {
        log_perror("udp-out setsockopt SO_PRIORITY");
        return false;
    }

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
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;

        if (uid->sport) {
            addr6.sin6_addr = in6addr_any;
            addr6.sin6_port = htons(uid->sport);
            if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
                log_perror("udp-out bind sock udp6 src port %u", uid->sport);
                close(sock);
                return false;
            }
        }

        int val = IPV6_PMTUDISC_PROBE;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &val, sizeof(val)) < 0) {
            log_perror("udp-out %s setsockopt IP_MTU_DISCOVER", iface->name);
            close(sock);
            return false;
        }

        addr6.sin6_addr = uid->dstip.v6;
        addr6.sin6_port = htons(uid->dport);
        if (connect(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
            log_perror("udp-out %s connect to '%s' port %u", iface->name, uid->dst_ip, uid->dport);
            close(sock);
            return false;
        }

        int tos = (uid->priority & 7) << 5;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(int)) < 0) {
            log_perror("udp-out setsockopt IPV6_TCLASS");
            close(sock);
            return false;
        }

        // it seems that setting IPv6 MTU discovery mode is also needed here
        val = IPV6_PMTUDISC_PROBE;
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &val, sizeof(val)) < 0) {
            log_perror("udp-out setsockopt IP_MTU_DISCOVER");
            close(sock);
            return false;
        }
    } else {
        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;

        if (uid->sport) {
            addr4.sin_addr.s_addr = htonl(INADDR_ANY);
            addr4.sin_port = htons(uid->sport);
            if (bind(sock, (struct sockaddr*)&addr4, sizeof(addr4)) < 0) {
                log_perror("udp-out bind sock udp4 src port %u", uid->sport);
                close(sock);
                return false;
            }
        }

        int val = IP_PMTUDISC_PROBE;
        if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
            log_perror("udp-out setsockopt IP_MTU_DISCOVER");
            close(sock);
            return false;
        }

        addr4.sin_addr = uid->dstip.v4;
        addr4.sin_port = htons(uid->dport);
        if (connect(sock, (struct sockaddr*)&addr4, sizeof(addr4)) < 0) {
            log_perror("udp-out %s connect to '%s' port %u", iface->name, uid->dst_ip, uid->dport);
            close(sock);
            return false;
        }

        int tos = (uid->priority & 7) << 5;
        if (setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0) {
            log_perror("udp-out setsockopt IP_TOS");
            close(sock);
            return false;
        }

        // it seems that setting IPv4 MTU discovery mode is also needed here
        val = IP_PMTUDISC_PROBE;
        if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)) < 0) {
            log_perror("udp-out setsockopt IP_MTU_DISCOVER");
            close(sock);
            return false;
        }
    }

    // enable so_txtime sockopt on the socket because delay offload appeared in actions
    if (iface->delay_offload) {
        if (!enable_so_txtime(sock, "udp-out", iface->ifname, false)) {
            return false;
        }
    }

    uid->sock = sock;

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

    log_info("Udp-out interface %s on device %s destination %s port %u", iface->name, iface->ifname, uid->dst_ip, uid->dport);
    iface->dropstat_cntr = 0;
    iface->dropstat_last_warn = 0;
    iface->state = IFS_OPEN;
    return true;
}

static bool udpout_close(struct Interface *iface)
{
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;
    stop_monitoring_error_queue(uid->errq_monitor);
    close(uid->sock);
    free(uid->dst_ip);
    free(uid->ifname);
    pthread_mutex_destroy(&uid->mutex);
    free(uid);
    log_info("Udp-out interface %s closed", iface->name);
    return true;
}

static void udpout_srcport_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;
    struct Value val = {&uid->sport, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void udpout_dstport_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;
    struct Value val = {&uid->dport, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void udpout_dstip_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;
    struct Value val = {&uid->dstip, 0, uid->family == AF_INET6 ? 128u : 32u};
    consumer(consumer_state, &val, p);
}

static value_producer *udpout_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;

    if (strcmp(property, "srcport") == 0) {
        if (target_type != FT_NUMBER) {
            log_error("udpout_get_property_reader 'srcport' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            log_error("udpout_get_property_reader 'srcport' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_srcport_producer;
    } else if (strcmp(property, "dstport") == 0) {
        if (target_type != FT_NUMBER) {
            log_error("udpout_get_property_reader 'dstport' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            log_error("udpout_get_property_reader 'dstport' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_dstport_producer;
    } else if (strcmp(property, "dstip") == 0) {
        enum ProtocolFieldType ftype = uid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = uid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            log_error("udpout_get_property_reader 'srcip' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            log_error("udpout_get_property_reader 'srcip' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_dstip_producer;
    }

    log_error("udpout_get_property_reader unknown property '%s'", property);
    return NULL;
}

struct Interface *new_udp_out_interface(const char *name, const char *ifname,
        unsigned src_port, const char *dst_ip, unsigned dst_port, unsigned priority)
{
    _NEW_IFACE(IF_UDP_OUT);
    iface->ifname = strdup(ifname);
    iface->recv = udpout_recv;
    iface->send = udpout_send;
    iface->open = udpout_open;
    iface->close_ = udpout_close;
    iface->get_property_reader = udpout_get_property_reader;

    struct in_addr dst4;
    struct in6_addr dst6;
    int family;

    if (inet_pton(AF_INET, dst_ip, &dst4) == 1) {
        family = AF_INET;
    } else if (inet_pton(AF_INET6, dst_ip, &dst6) == 1) {
        family = AF_INET6;
    } else {
        if (strcmp(dst_ip, "ipv4") == 0) {
            family = AF_INET;
            dst_ip = "<none>";
        } else if (strcmp(dst_ip, "ipv6") == 0) {
            family = AF_INET6;
            dst_ip = "<none>";
        } else {
            log_error("udp-out invalid dstip '%s'", dst_ip);
            free(iface);
            return NULL;
        }
    }

    iface->state = IFS_INIT;

    struct UdpOutIfData *uid = calloc_struct(UdpOutIfData);
    iface->iface_private = uid;
    uid->ifname = strdup(ifname);
    uid->dst_ip = strdup(dst_ip);
    uid->sport = src_port;
    uid->dport = dst_port;
    uid->family = family;
    uid->priority = priority;
    if (family == AF_INET6) {
        uid->dstip.v6 = dst6;
    } else {
        uid->dstip.v4 = dst4;
    }
    pthread_mutex_init(&uid->mutex, NULL);

    return iface;
}

bool udp_out_set_dst(struct Interface *iface, const char *dst_ip, unsigned dst_port)
{
    struct UdpOutIfData *uid = (struct UdpOutIfData *)iface->iface_private;

    if (iface->type != IF_UDP_OUT) {
        log_error("udp_out_set_dst: this interface is %s", iface_type_str(iface->type));
        return false;
    }

    if (dst_port > 0xffff) {
        log_error("udp_out_set_dst: invalid port %u", dst_port);
    }

    if (dst_port == uid->dport && strcmp(dst_ip, uid->dst_ip) == 0) {
        log_debug("udp_out_set_dst: no change");
        return true;
    }

    struct in_addr dst4;
    struct in6_addr dst6;
    int family;
    bool specified = false;

    if (inet_pton(AF_INET, dst_ip, &dst4) == 1) {
        family = AF_INET;
        specified = true;
    } else if (inet_pton(AF_INET6, dst_ip, &dst6) == 1) {
        family = AF_INET6;
        specified = true;
    } else {
        if (strcmp(dst_ip, "<none>") == 0) {
            family = uid->family;
        } else {
            log_error("udp_out_set_dst: invalid dstip '%s'", dst_ip);
            return false;
        }
    }

    if (family != uid->family) {
        log_error("udp_out_set_dst: my family is %s notification is %s",
                family==AF_INET6?"ipv6":"ipv4", uid->family==AF_INET6?"ipv6":"ipv4");
        return false;
    }

    if (iface->state == IFS_INIT) {
        free(uid->dst_ip);
        uid->dst_ip = strdup(dst_ip);
        uid->dport = dst_port;
        if (family == AF_INET6) {
            uid->dstip.v6 = dst6;
        } else {
            uid->dstip.v4 = dst4;
        }
        if (uid->opened) {
            // this means we wanted to open but had no address
            return udpout_open(iface); // try again with the newly acquired address
        }
        return true;
    } else {
        if (specified) {
            // connect to new address
            struct sockaddr *sa;
            unsigned sa_len;
            struct sockaddr_in6 addr6;
            struct sockaddr_in addr4;

            if (family == AF_INET6) {
                sa = (struct sockaddr*)&addr6;
                sa_len = sizeof(addr6);
                memset(&addr6, 0, sizeof(addr6));
                addr6.sin6_family = AF_INET6;
                addr6.sin6_addr = dst6;
                addr6.sin6_port = htons(dst_port);
            } else {
                sa = (struct sockaddr*)&addr4;
                sa_len = sizeof(addr4);
                memset(&addr4, 0, sizeof(addr4));
                addr4.sin_family = AF_INET;
                addr4.sin_addr = dst4;
                addr4.sin_port = htons(dst_port);
            }

            pthread_mutex_lock(&uid->mutex);
            if (connect(uid->sock, sa, sa_len) < 0) {
                pthread_mutex_unlock(&uid->mutex);
                log_error("TODO");
                return false;
            }
            pthread_mutex_unlock(&uid->mutex);

            log_info("udp-out %s changed destination to %s port %u",
                    iface->name, dst_ip, dst_port);

            if (family == AF_INET6) {
                uid->dstip.v6 = dst6;
            } else {
                uid->dstip.v4 = dst4;
            }
            uid->dport = dst_port;
        } else {
            log_info("udp-out %s destination has lost its address", iface->name);
            pthread_mutex_lock(&uid->mutex);
            uid->errq_monitor = stop_monitoring_error_queue(uid->errq_monitor);
            close(uid->sock);
            uid->sock = 0;
            iface->state = IFS_INIT;
            pthread_mutex_unlock(&uid->mutex);
        }

        free(uid->dst_ip);
        uid->dst_ip = strdup(dst_ip);
        uid->dport = dst_port;
        return true;
    }
}
