// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_ip.h"
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
#include <netinet/in.h>
#include <net/if.h> /* struct ifreq */
#include <linux/if_ether.h>
#include <linux/if_packet.h> /* struct sockaddr_ll TODO netpacket/packet.h? */
#include <ifaddrs.h>

struct IpIfData {
    int sock4;
    int sock6;
    int ifindex;
    int mtu;
    struct in_addr ipv4;
    struct in6_addr ipv6;
};

static struct Packet *ip_recv(struct Interface *iface)
{
    fprintf(stderr, "ip interface %s recv how??\n", iface->name);
    return NULL;
}

static bool ip_send(struct Interface *iface, struct Packet *p)
{
    struct IpIfData *iid = iface->iface_private;
    if (p->header_count < 1) {
        fprintf(stderr, "ip %s send: packet doesn't have headers\n", iface->name);
        return false;
    }
    if (p->headers[0].type == PROTO_ID_IPv4) {
        struct sockaddr_in socket_address;
        memset(&socket_address, 0, sizeof(struct sockaddr_in));
        socket_address.sin_family = AF_INET;
        memcpy(&socket_address.sin_addr.s_addr,  p->buf + p->headers[0].start + 16, 4);
        return iface_common_send(iface, p, iid->sock4, &socket_address, sizeof(socket_address));
    } else if (p->headers[0].type == PROTO_ID_IPv6) {
        struct sockaddr_ll socket_address;
        memset(&socket_address, 0, sizeof(struct sockaddr_ll));
        socket_address.sll_family = AF_PACKET;
        socket_address.sll_protocol = htons (ETH_P_IPV6);
        socket_address.sll_ifindex = iid->ifindex;
        socket_address.sll_halen = ETH_ALEN;
        //TODO can we do something about this broadcast?
        memset(socket_address.sll_addr, 0xff, ETH_ALEN);
        return iface_common_send(iface, p, iid->sock6, &socket_address, sizeof(socket_address));
    } else {
        fprintf(stderr, "ip %s send: first header of the packet is not IP\n", iface->name);
        return false;
    }
}

static bool ip_open(struct Interface *iface)
{
    struct IpIfData *iid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        fprintf(stderr, "open ip interface %s: already opened\n", iface->name);
        return false;
    }
    if (iface->parsetree == NULL) {
        fprintf(stderr, "open ip interface %s: no parsetree, expect trouble\n", iface->name);
        //TODO fatal?
    }

    // the dummy protocol type signals that we don't want to receive things
    int sock4 = socket(AF_INET, SOCK_RAW, IPPROTO_BEETPH);
    if (sock4 < 0) {
        perror("socket4");
        return false;
    }

    struct ifreq if_mtu, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, iface->ifname, IFNAMSIZ-1);
    strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
    if (ioctl(sock4, SIOCGIFMTU, &if_mtu) < 0) {
        perror("SIOCGIFMTU");
        close(sock4);
        return false;
    }
    if (ioctl(sock4, SIOCGIFINDEX, &if_idx) < 0) {
        perror("SIOCGIFINDEX");
        close(sock4);
        return false;
    }
    iid->mtu = if_mtu.ifr_mtu;
    iid->ifindex = if_idx.ifr_ifindex;

    if (setsockopt(sock4, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        perror("ip setsockopt SO_BINDTODEVICE");
        close(sock4);
        return false;
    }

    int enable = 1;
    if (setsockopt(sock4, IPPROTO_IP, IP_HDRINCL, &enable, sizeof(enable)) < 0) {
        perror("ip setsockopt IP_HDRINCL");
        close(sock4);
        return false;
    }

    // note: IP_HDRINCL has no equivalent for IPv6, we must use L2 socket
    // proto=0 means no reception
    int sock6 = socket(AF_PACKET, SOCK_DGRAM, 0);
    if (sock6 < 0) {
        perror("socket6");
        close(sock4);
        return false;
    }

    if (setsockopt(sock6, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        perror("ip setsockopt SO_BINDTODEVICE");
        close(sock4);
        close(sock6);
        return false;
    }

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) < 0) {
        perror("ip getifaddrs");
        close(sock4);
        close(sock6);
        return false;
    }

    bool srcip4_set = false;
    bool srcip6_set = false;
    for (struct ifaddrs *ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        int family = ifa->ifa_addr->sa_family;

        if (strcmp(ifa->ifa_name, iface->ifname) == 0) {
            //print_ifaddrs(ifa);
            if (family == AF_INET6) {
                if (srcip6_set) continue;
                struct in6_addr *a6 = &((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
                if (IN6_IS_ADDR_LINKLOCAL(a6)) continue;
                iid->ipv6 = ((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
                srcip6_set = true;
            } else if (family == AF_INET) {
                if (srcip4_set) continue;
                iid->ipv4 = ((struct sockaddr_in*)(ifa->ifa_addr))->sin_addr;
                srcip4_set = true;
            }
        }
    }

    freeifaddrs(ifaddr);
    if (!srcip4_set && !srcip6_set) {
        fprintf(stderr, "open ip interface %s: no address on interface %s\n", iface->name, iface->ifname);
        close(sock4);
        close(sock6);
        return false;
    }
    //TODO see the comment in if_udp_in.c


    iid->sock4 = sock4;
    iid->sock6 = sock6;
    iface->dropstat_cntr = 0;
    iface->dropstat_last_warn = 0;
    iface->state = IFS_OPEN;

    return true;
}

static bool ip_close(struct Interface *iface)
{
    struct IpIfData *iid = iface->iface_private;
    close(iid->sock4);
    close(iid->sock6);
    free(iid);
    return true;
}

static void ip_ipv4_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct IpIfData *iid = iface->iface_private;
    struct Value val = {&iid->ipv4, 0, 32};
    consumer(consumer_state, &val, p);
}

static void ip_ipv6_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct IpIfData *iid = iface->iface_private;
    struct Value val = {&iid->ipv6, 0, 128};
    consumer(consumer_state, &val, p);
}

static value_producer *ip_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    struct IpIfData *iid = iface->iface_private;
    (void)iid;

    //TODO check if the interface has such an address
    //      problem: we are between init and open

    if (strcmp(property, "srcip") == 0) {
        if (target_type == FT_IPV4ADDRESS) {
            if ((target->bitoffset % 8) || (target->bitcount != 4*8)) {
                fprintf(stderr, "ip_get_property_reader 'srcip' target position %u %u invalid\n",
                        target->bitoffset, target->bitcount);
                return NULL;
            }
            return ip_ipv4_producer;
        } else if (target_type == FT_IPV6ADDRESS) {
            if ((target->bitoffset % 8) || (target->bitcount != 16*8)) {
                fprintf(stderr, "ip_get_property_reader 'srcip' target position %u %u invalid\n",
                        target->bitoffset, target->bitcount);
                return NULL;
            }
            return ip_ipv6_producer;
        } else {
            fprintf(stderr, "ip_get_property_reader 'srcip' target type %d invalid\n", target_type);
            return NULL;
        }
    }

    fprintf(stderr, "ip_get_property_reader unknown property '%s'\n", property);
    return NULL;
}

bool init_ip_interface(struct Interface *iface, const char *name, const char *ifname)
{
    //printf("init_ip_interface %s %s\n", name, ifname);
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    iface->ifname = strdup(ifname);
    iface->type = IF_IP;
    iface->state = IFS_INIT;
    iface->recv = ip_recv;
    iface->send = ip_send;
    iface->open = ip_open;
    iface->close_ = ip_close;
    iface->get_property_reader = ip_get_property_reader;

    struct IpIfData *iid = calloc_struct(IpIfData);
    iface->iface_private = iid;

    return true;
}
