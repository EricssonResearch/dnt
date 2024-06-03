// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "log.h"
#include "if_ip.h"
#include "if_utils.h"
#include "interface.h"
#include "packet.h"
#include "parsetree.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h> /* struct ifreq */
#include <linux/if_ether.h>
#include <linux/if_packet.h> /* struct sockaddr_ll TODO netpacket/packet.h? */
#include <ifaddrs.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

struct IpIfData {
    int sock4;
    int sock6;
    int ifindex;
    int mtu;
    struct in_addr ipv4;
    struct in6_addr ipv6;
};

static void process_msghdr(struct msghdr *msg, struct Packet *p, void *userdata)  // ez lehet nem fog kelleni
{
    (void)userdata;
    (void)p;

    for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(msg); cmsg; cmsg=CMSG_NXTHDR(msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_PACKET:
                if (cmsg->cmsg_type == IPV6_PKTINFO) {
                    ;
                }
                break;
        }
    }
}

static bool ip_recv(struct Interface *iface)
{
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;
    (void)iid;


    struct Packet *p = iface_common_recv(iface, process_msghdr, NULL);
    if (p == NULL) return false;

    unsigned short *ethertype = (unsigned short*)(p->buf + p->start + 12);

    //log_info("ip packet: %x", htons(*ethertype));

    if(htons(*ethertype) != ETH_P_IPV6) {
        packet_logcat(p, "%s %u not IPv6 (%x), drop.", iface->name, p->len, htons(*ethertype));
        return delete_packet(p);
    }

    // Skip Eth header
    p->start += 14;
    p->len -= 14;

/* hex dump packet - just debug

    char dump_str[4000], ch[5];
    sprintf(dump_str,"\n");
    unsigned char *pp=p->buf + p->start;;
    for(int i=1; i<=128; i++){
        sprintf(ch, " %02x", *pp);
        strcat(dump_str, ch);
        pp++;
        if(i%16==0)
            strcat(dump_str, "\n");
    }
    strcat(dump_str, "\n");
    log_info("ip packet: %s", dump_str);
*/

    packet_logcat(p, "%s %u ", iface->name, p->len);
    return iface_common_process(iface, p);
}

static bool ip_send(struct Interface *iface, struct Packet *p)
{
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;

    if (p->header_count < 1) {
        log_error("ip %s send: packet doesn't have headers", iface->name);
        return false;
    }

    if (p->headers[0].type == PROTO_ID_IPv4) {
        struct sockaddr_in socket_address;
        memset(&socket_address, 0, sizeof(struct sockaddr_in));
        socket_address.sin_family = AF_INET;
        memcpy(&socket_address.sin_addr.s_addr,  p->buf + p->headers[0].start + 16, 4);
        return iface_common_send(iface, p, iid->sock4, &socket_address, sizeof(socket_address));
    } else if (p->headers[0].type == PROTO_ID_IPv6) {
        // update packet length
        unsigned short *length = (unsigned short*)(p->buf + p->headers[0].start + 4);
        *length = htons(p->len-40);     // IPv6 header length
        // if srcip is zero, set source addr.
        unsigned int *p_ip = (unsigned int*)(p->buf + p->headers[0].start + 8);
        if((*p_ip+*(p_ip+1)+*(p_ip+2)+*(p_ip+3))==0)
            memcpy(p_ip, &iid->ipv6, 16);
        // fill in some Eth header to send
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
        log_error("ip %s send: first header of the packet is not IP", iface->name);
        return false;
    }
}

static bool ip_open(struct Interface *iface)
{
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open ip interface %s: already opened", iface->name);
        return false;
    }

    // the dummy protocol type signals that we don't want to receive things
    int sock4 = socket(AF_INET, SOCK_RAW, IPPROTO_BEETPH);
    if (sock4 < 0) {
        log_perror("create socket4 for %s", iface->name);
        return false;
    }

    struct ifreq if_mtu, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, iface->ifname, IFNAMSIZ-1);
    strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
    if (ioctl(sock4, SIOCGIFMTU, &if_mtu) < 0) {
        log_perror("SIOCGIFMTU for %s on %s", iface->name, iface->ifname);
        close(sock4);
        return false;
    }
    if (ioctl(sock4, SIOCGIFINDEX, &if_idx) < 0) {
        log_perror("SIOCGIFINDEX for %s on %s", iface->name, iface->ifname);
        close(sock4);
        return false;
    }
    iid->mtu = if_mtu.ifr_mtu;
    iid->ifindex = if_idx.ifr_ifindex;

    if (setsockopt(sock4, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        log_perror("ip4 setsockopt SO_BINDTODEVICE for %s on %s", iface->name, iface->ifname);
        close(sock4);
        return false;
    }

    int enable = 1;
    if (setsockopt(sock4, IPPROTO_IP, IP_HDRINCL, &enable, sizeof(enable)) < 0) {
        log_perror("ip4 setsockopt IP_HDRINCL for %s", iface->name);
        close(sock4);
        return false;
    }

    // enable so_txtime sockopt on the socket because delay offload appeared in actions
    if (iface->delay_offload)
        if (!enable_so_txtime(sock4, "ip", iface->ifname, false))
            return false;


    // Setup receiver sockets
    int sock_raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));  // ez vesz mindent
    if (sock_raw < 0) {
        log_perror("create socket raw for %s", iface->name);
        close(sock4);
        return false;
    }
    if (setsockopt(sock_raw, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        log_perror("ip6 setsockopt SO_BINDTODEVICE for %s on %s", iface->name, iface->ifname);
        close(sock4);
        close(sock_raw);
        return false;
    }

    // note: IP_HDRINCL has no equivalent for IPv6, we must use L2 socket
    // proto=0 means no reception
    int sock6 = socket(AF_PACKET, SOCK_DGRAM, 0);
    if (sock6 < 0) {
        log_perror("create socket6 for %s", iface->name);
        close(sock4);
        return false;
    }

    if (setsockopt(sock6, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        log_perror("ip6 setsockopt SO_BINDTODEVICE for %s on %s", iface->name, iface->ifname);
        close(sock4);
        close(sock6);
        close(sock_raw);
        return false;
    }

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) < 0) {
        log_perror("ip getifaddrs for %s", iface->name);
        close(sock4);
        close(sock6);
        close(sock_raw);
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
        log_error("open ip interface %s: no address on interface %s", iface->name, iface->ifname);
        close(sock4);
        close(sock6);
        close(sock_raw);
        return false;
    }
    //TODO see the comment in if_udp_in.c


    iid->sock4 = sock4;
    iid->sock6 = sock6;
    iface->recvfd = sock_raw;
    iface->dropstat_cntr = 0;
    iface->dropstat_last_warn = 0;
    iface->state = IFS_OPEN;
    log_info("IP interface %s on device %s", iface->name, iface->ifname);

    return true;
}

static bool ip_close(struct Interface *iface)
{
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;
    close(iface->recvfd);
    close(iid->sock4);
    close(iid->sock6);
    free(iid);
    log_info("IP interface %s closed", iface->name);
    return true;
}

static void ip_ipv4_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;
    struct Value val = {&iid->ipv4, 0, 32};
    consumer(consumer_state, &val, p);
}

static void ip_ipv6_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;
    struct Value val = {&iid->ipv6, 0, 128};
    consumer(consumer_state, &val, p);
}

static value_producer *ip_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    struct IpIfData *iid = (struct IpIfData *)iface->iface_private;
    (void)iid;

    //TODO check if the interface has such an address
    //      problem: we are between init and open

    if (strcmp(property, "srcip") == 0) {
        if (target_type == FT_IPV4ADDRESS) {
            if ((target->bitoffset % 8) || (target->bitcount != 4*8)) {
                log_error("ip_get_property_reader 'srcip' target position %u %u invalid",
                        target->bitoffset, target->bitcount);
                return NULL;
            }
            return ip_ipv4_producer;
        } else if (target_type == FT_IPV6ADDRESS) {
            if ((target->bitoffset % 8) || (target->bitcount != 16*8)) {
                log_error("ip_get_property_reader 'srcip' target position %u %u invalid",
                        target->bitoffset, target->bitcount);
                return NULL;
            }
            return ip_ipv6_producer;
        } else {
            log_error("ip_get_property_reader 'srcip' target type %d invalid", target_type);
            return NULL;
        }
    }

    log_error("ip_get_property_reader unknown property '%s'", property);
    return NULL;
}

struct Interface *new_ip_interface(const char *name, const char *ifname)
{
    _NEW_IFACE(IF_IP);
    iface->ifname = strdup(ifname);
    iface->recv = ip_recv;
    iface->send = ip_send;
    iface->open = ip_open;
    iface->close_ = ip_close;
    iface->get_property_reader = ip_get_property_reader;

    struct IpIfData *iid = calloc_struct(IpIfData);
    iface->iface_private = iid;

    iface->parsetree_ = new_parsetree(iface);

    return iface;
}
