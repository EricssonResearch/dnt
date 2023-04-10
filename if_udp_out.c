
#include "if_udp_out.h"
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
#include <netdb.h> /* getaddrinfo() */

struct UdpOutIfData {
    int ifindex;
    int mtu;
    unsigned port;
    int family;
    unsigned priority;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } dstip;
};

static struct Packet *udpout_recv(struct Interface *iface)
{
    struct Packet *p = iface_common_recv(iface, NULL, NULL);
    if (p) {
        printf("udp-out %s recv %u dropping\n", iface->name, p->len);
        return delete_packet(p);
    } else {
        return NULL;
    }
}

static bool udpout_send(struct Interface *iface, struct Packet *p)
{
    // our socket is connected, so no dst
    return iface_common_send(iface, p, iface->recvfd, NULL, 0);
}

static bool udpout_open(struct Interface *iface)
{
    struct UdpOutIfData *uid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        fprintf(stderr, "open udp-out interface %s: already opened\n", iface->name);
        return false;
    }

    struct ifreq if_mtu, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, iface->ifname, strlen(iface->ifname));
    strncpy(if_idx.ifr_name, iface->ifname, strlen(iface->ifname));
    if (ioctl(iface->recvfd, SIOCGIFMTU, &if_mtu) < 0) {
        perror("udp-out SIOCGIFMTU");
        return false;
    }
    if (ioctl(iface->recvfd, SIOCGIFINDEX, &if_idx) < 0) {
        perror("udp-out SIOCGIFINDEX");
        return false;
    }
    uid->mtu = if_mtu.ifr_mtu;
    uid->ifindex = if_idx.ifr_ifindex;

    if (setsockopt(iface->recvfd, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        perror("udp-out setsockopt SO_BINDTODEVICE");
        return false;
    }

    if (uid->family == AF_INET6) {
        int tos = (uid->priority & 7) << 5;
        if (setsockopt(iface->recvfd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(int)) < 0) {
            perror("udp-out setsockopt IPV6_TCLASS");
            return false;
        }
    } else {
        int tos = (uid->priority & 7) << 5;
        if (setsockopt(iface->recvfd, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0) {
            perror("udp-out setsockopt IP_TOS");
            return false;
        }
    }

    if (setsockopt(iface->recvfd, SOL_SOCKET, SO_PRIORITY, &uid->priority, sizeof(int)) < 0) {
        perror("udp-out setsockopt SO_PRIORITY");
        return false;
    }

    if (setsockopt(iface->recvfd, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        perror("udp-out setsockopt SO_BINDTODEVICE");
        return false;
    }

    iface->state = IFS_OPEN;
    return true;
}

static bool udpout_close(struct Interface *iface)
{
    struct UdpOutIfData *uid = iface->iface_private;
    close(iface->recvfd);
    free(uid);
    return true;
}

static void udpout_port_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct UdpOutIfData *uid = iface->iface_private;
    struct Value val = {&uid->port, 0, 2*8};
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

    if (strcmp(property, "port") == 0) {
        if (target_type != FT_NUMBER) {
            fprintf(stderr, "udpout_get_property_reader 'port' target type %d invalid\n", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            fprintf(stderr, "udpout_get_property_reader 'port' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_port_producer;
    } else if (strcmp(property, "dstip") == 0) {
        enum ProtocolFieldType ftype = uid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = uid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            fprintf(stderr, "udpout_get_property_reader 'srcip' target type %d invalid\n", target_type);
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            fprintf(stderr, "udpout_get_property_reader 'srcip' target position %u %u invalid\n",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpout_dstip_producer;
    }

    fprintf(stderr, "udpout_get_property_reader unknown property '%s'\n", property);
    return NULL;
}

bool init_udp_out_interface(struct Interface *iface, const char *name, const char *ifname,
        unsigned port, const char *dst_ip, unsigned priority)
{
    printf("init_udp_out_interface %s %s\n", name, ifname);
    bzero(iface, sizeof(*iface));
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
    sprintf(port_str, "%u", port);
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0; //TODO AI_NUMERICHOST?
    int err = getaddrinfo(dst_ip, port_str, &hints, &result);

    if (err) {
        fprintf(stderr, "udp-out invalid dstip '%s'\n", dst_ip);
        return false;
    }

    int sock = -1;
    for (rp=result; rp!=NULL; rp=rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        perror("udp-out connect");
        close(sock);
        sock = -1;
    }

    if (sock < 0) {
        fprintf(stderr, "udp-out can't make socket with dstip '%s'\n", dst_ip);
        freeaddrinfo(result);
        return false;
    }

    iface->recvfd = sock;
    iface->state = IFS_INIT;

    struct UdpOutIfData *uid = calloc_struct(UdpOutIfData);
    iface->iface_private = uid;
    uid->port = port;
    uid->family = rp->ai_family;
    if (uid->family == AF_INET6) {
        uid->dstip.v6 = ((struct sockaddr_in6*)(rp->ai_addr))->sin6_addr;
    } else {
        uid->dstip.v4 = ((struct sockaddr_in*)(rp->ai_addr))->sin_addr;
    }
    uid->priority = priority;
    freeaddrinfo(result);

    return true;
}
