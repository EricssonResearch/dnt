
#include "if_udp_in.h"
#include "if_utils.h"
#include "interface.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>

struct UdpInIfData {
    int ifindex;
    int mtu;
    unsigned port;
    int family;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } srcip;
};

static struct Packet *udpin_recv(struct Interface *iface)
{
    struct UdpInIfData *uid = iface->iface_private;
    (void)uid;

    return iface_common_recv(iface, NULL, NULL);
}

static bool udpin_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    fprintf(stderr, "udp-in interface %s refusing to send packet\n", iface->name);
    return false;
}

static bool udpin_open(struct Interface *iface)
{
    struct UdpInIfData *uid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        fprintf(stderr, "open udp-in interface %s: already opened\n", iface->name);
        return false;
    }
    if (iface->parsetree == NULL) {
        fprintf(stderr, "open udp-in interface %s: no parsetree, expect trouble\n", iface->name);
        //TODO fatal?
    }

    int sock = socket(uid->family, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("udp-in setsockopt SO_REUSEADDR");
        close(sock);
        return false;
    }

    struct ifreq if_mtu, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, iface->ifname, IFNAMSIZ);
    strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFMTU, &if_mtu) < 0) {
        perror("udp-in SIOCGIFMTU");
        close(sock);
        return false;
    }
    if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
        perror("udp-in SIOCGIFINDEX");
        close(sock);
        return false;
    }
    uid->mtu = if_mtu.ifr_mtu;
    uid->ifindex = if_idx.ifr_ifindex;

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) < 0) {
        perror("udp-in getifaddrs");
        close(sock);
        return false;
    }

    bool srcip_set = false;
    for (struct ifaddrs *ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != uid->family) continue;

        if (family == AF_INET6) {
            struct in6_addr *a6 = &((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
            if (IN6_IS_ADDR_LINKLOCAL(a6)) continue;
            uid->srcip.v6 = ((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
            srcip_set = true;
            break;
        } else {
            uid->srcip.v4 = ((struct sockaddr_in*)(ifa->ifa_addr))->sin_addr;
            srcip_set = true;
            break;
        }
    }

    freeifaddrs(ifaddr);
    if (!srcip_set) {
        fprintf(stderr, "open udp-in interface %s: no address on interface %s\n", iface->name, iface->ifname);
        close(sock);
        return false;
    }
    //TODO notification for changes in the interface address
    //      see https://olegkutkov.me/2018/02/14/monitoring-linux-networking-state-using-netlink/

    if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        perror("udp-in setsockopt SO_BINDTODEVICE");
        close(sock);
        return false;
    }

    if (uid->family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(uid->port);
        if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
            perror("udp-in bind sock udp6");
            return false;
        }
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(uid->port);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("udp-in bind sock udp");
            return false;
        }
    }

    enable_rx_tstamp(sock, "udp-in", iface->ifname/*, HWTSTAMP_FILTER_ALL*/);

    iface->recvfd = sock;
    iface->state = IFS_OPEN;
    return true;
}

static bool udpin_close(struct Interface *iface)
{
    struct UdpInIfData *uid = iface->iface_private;
    close(iface->recvfd);
    free(uid);
    return true;
}

static void udpin_port_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct UdpInIfData *uid = iface->iface_private;
    struct Value val = {&uid->port, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void udpin_srcip_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct UdpInIfData *uid = iface->iface_private;
    struct Value val = {&uid->srcip, 0, uid->family == AF_INET6 ? 128 : 32};
    consumer(consumer_state, &val, p);
}

static value_producer *udpin_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    struct UdpInIfData *uid = iface->iface_private;

    if (strcmp(property, "port") == 0) {
        if (target_type != FT_NUMBER) {
            fprintf(stderr, "udpin_get_property_reader 'port' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            fprintf(stderr, "udpin_get_property_reader 'port' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpin_port_producer;
    } else if (strcmp(property, "srcip") == 0) {
        enum ProtocolFieldType ftype = uid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = uid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            fprintf(stderr, "udpin_get_property_reader 'srcip' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            fprintf(stderr, "udpin_get_property_reader 'srcip' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpin_srcip_producer;
    }

    fprintf(stderr, "udpin_get_property_reader unknown property '%s'\n", property);
    return NULL;
}

bool init_udp_in_interface(struct Interface *iface, const char *name, const char *ifname,
        unsigned port, unsigned ipversion)
{
    //printf("init_udp_in_interface %s %s\n", name, ifname);
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    iface->ifname = strdup(ifname);
    iface->type = IF_UDP_IN;
    iface->state = IFS_INIT;
    iface->recv = udpin_recv;
    iface->send = udpin_send;
    iface->open = udpin_open;
    iface->close_ = udpin_close;
    iface->get_property_reader = udpin_get_property_reader;

    struct UdpInIfData *uid = calloc_struct(UdpInIfData);
    iface->iface_private = uid;
    uid->port = port;
    uid->family = ipversion == 6 ? AF_INET6 : AF_INET;

    return true;
}

