// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_eth.h"
#include "if_utils.h"
#include "interface.h"
#include "packet.h"
#include "parsetree.h"
#include "protocol.h"
#include "utils.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h> /* close() */
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h> /* struct ifreq */
#include <net/ethernet.h> /* struct ether_addr */

#include <linux/if_ether.h> /* ETH_P_ALL */
#include <linux/if_packet.h> /* struct sockaddr_ll, PACKET_AUXDATA TODO netpacket/packet.h? */
#include <linux/filter.h> /* eBPF */

DEFAULT_LOGGING_MODULE(INTERFACE, WARNING);

struct EthIfData {
    int ifindex;
    int sockfd[8];
    bool pcp_used[8]; // indicate whether we have a socket open for this priority
    int mtu;
    struct ether_addr mac;
};

static void restore_vlan(struct msghdr *msg, struct Packet *p, void *userdata)
{
    (void)userdata;

    for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(msg); cmsg; cmsg=CMSG_NXTHDR(msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_PACKET:
                if (cmsg->cmsg_type == PACKET_AUXDATA) {
                    struct tpacket_auxdata *aux = (struct tpacket_auxdata *)CMSG_DATA(cmsg);
                    //printf("AUX 0x%.08x len %u VLAN 0x%.04x EtherType 0x%.04x\n",
                    //        aux->tp_status, aux->tp_len, aux->tp_vlan_tci, aux->tp_vlan_tpid);

                    if (aux->tp_status & TP_STATUS_VLAN_VALID) {
                        //printf("restoring vlan header in the packet\n");
                        memmove(p->buf + p->start - 4, p->buf + p->start, 2*6);
                        p->start -= 4;
                        p->len += 4;
                        uint16_t *vlan = (uint16_t*)(p->buf + p->start + 2*6);
                        vlan[0] = htons(aux->tp_vlan_tpid);
                        vlan[1] = htons(aux->tp_vlan_tci);
                    }
                }
                break;
        }
    }
}

static struct Packet *eth_recv(struct Interface *iface)
{
    struct EthIfData *eid = iface->iface_private;
    (void)eid;

    struct Packet *p = iface_common_recv(iface, restore_vlan, NULL);
    if (p == NULL) return NULL;

    uint16_t *p_vlan = (uint16_t*)(p->buf + p->start + 2*6);
    unsigned short ethertype = ntohs(*p_vlan);
    if (ethertype != ETH_P_8021Q && ethertype != ETH_P_8021AD) {
        //printf("adding default cvlan to untagged packet\n");
        memmove(p->buf + p->start - 4, p->buf + p->start, 2*6);
        p->start -= 4;
        p->len += 4;
        p_vlan = (uint16_t*)(p->buf + p->start + 2*6);
        p_vlan[0] = htons(ETH_P_8021Q);
        p_vlan[1] = 0;
    }

    return p;
}

static bool eth_send(struct Interface *iface, struct Packet *p)
{
    struct EthIfData *eid = iface->iface_private;

    if (p->header_count < 1) {
        log_error("eth %s send: packet doesn't have headers", iface->name);
        return false;
    }
    if (p->headers[0].type != PROTO_ID_ETH) {
        log_error("eth %s send: first header of the packet is not eth", iface->name);
        return false;
    }

    unsigned pcp = 0;
    if (p->header_count > 1) {
        if (p->headers[1].type == PROTO_ID_CVLAN || p->headers[1].type == PROTO_ID_SVLAN) {
            unsigned char *vlan = p->buf + p->headers[1].start;
            pcp = (*vlan) >> 5;
        }
    }
    //printf("eth %s sending with priority %u\n", iface->name, pcp);

    unsigned char *dst_mac = p->buf + p->headers[0].start;
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_ifindex = eid->ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, dst_mac, ETH_ALEN);

    return iface_common_send(iface, p, eid->sockfd[pcp], &socket_address, sizeof(socket_address));
}

static bool eth_open(struct Interface *iface)
{
    struct EthIfData *eid = iface->iface_private;

    if (iface->state != IFS_INIT) {
        log_error("open eth interface %s: already opened", iface->name);
        return false;
    }

    //TODO only need 1 socket if using the eBPF prioritizing filter
    for (unsigned i=0; i<8; i++) {
        int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (sock < 0) {
            log_perror("if_eth %s socket %d", iface->name, i);
            return false; //TODO cleanup on error
        }

        if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &i, sizeof(i)) < 0) {
            log_perror("setsockopt SO_PRIORITY");
            close(sock);
            return false; //TODO cleanup on error
        }

        if (i == 0) {
            struct ifreq if_mtu, if_mac, if_idx;
            memset(&if_mtu, 0, sizeof(struct ifreq));
            memset(&if_mac, 0, sizeof(struct ifreq));
            memset(&if_idx, 0, sizeof(struct ifreq));
            strncpy(if_mtu.ifr_name, iface->ifname, IFNAMSIZ-1);
            strncpy(if_mac.ifr_name, iface->ifname, IFNAMSIZ-1);
            strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
            if (ioctl(sock, SIOCGIFMTU, &if_mtu) < 0) {
                log_perror("SIOCGIFMTU");
                return false;
            }
            if (ioctl(sock, SIOCGIFHWADDR, &if_mac) < 0) {
                log_perror("SIOCGIFHWADDR");
                return false;
            }
            if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
                log_perror("SIOCGIFINDEX");
                return false;
            }
            eid->mtu = if_mtu.ifr_mtu;
            eid->ifindex = if_idx.ifr_ifindex;
            eid->mac = *((struct ether_addr *)&if_mac.ifr_hwaddr.sa_data);
            //      store this in an interface property

            int enable = 1;
            if (setsockopt(sock, SOL_PACKET, PACKET_AUXDATA, &enable, sizeof(enable)) < 0) {
                log_perror("setsockopt PACKET_AUXDATA");
                return false;
            }

            enable_rx_tstamp(sock, "eth", iface->ifname/*, HWTSTAMP_FILTER_ALL*/);

            // bind to device
            struct sockaddr_ll socket_address = {0};
            socket_address.sll_family = AF_PACKET;
            socket_address.sll_protocol = htons(ETH_P_ALL);
            socket_address.sll_ifindex = if_idx.ifr_ifindex;
            if (bind(sock, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
                log_perror("bind sock to iface");
                return false;
            }
            // set iface to promiscuous mode
            struct packet_mreq mreq;
            mreq.mr_type = PACKET_MR_PROMISC;
            mreq.mr_ifindex = if_idx.ifr_ifindex;
            mreq.mr_alen = 0;
            memset(&mreq.mr_address, 0, sizeof(mreq.mr_address));
            if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
                log_perror("setsockopt mreq promisc");
                return false;
            }

            // Block all PACKET_OUTGOING packets sent on other priority sockets from
            // coming back on socket 0
            struct sock_filter filter[] = {
                BPF_STMT(BPF_LD|BPF_B|BPF_ABS, (uint32_t)(SKF_AD_OFF + SKF_AD_PKTTYPE)),
                BPF_JUMP(BPF_JMP|BPF_K|BPF_JEQ, PACKET_OUTGOING, 0, 0x1),
                BPF_STMT(BPF_RET|BPF_K, 0x00000000),
                BPF_STMT(BPF_RET|BPF_K, 0x0000ffff)
            };
            struct sock_fprog bpf = {
                .len = (unsigned short) (sizeof(filter) / sizeof(filter[0])),
                .filter = filter
            };
            if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf)) < 0) {
                log_perror("setsockopt attach eBPF filter");
                return false;
            }
        } else {
            // set socket receive buffer to minimum (setting to zero will set it to minimum)
            int recvbuf_len = 0;
            socklen_t sl = sizeof(recvbuf_len);
            if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recvbuf_len, sl) < 0) {
                log_perror("setsockopt SO_RCVBUF");
                return false;
            }
            /*if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recvbuf_len, &sl) < 0) {
                log_perror("getsockopt SO_RCVBUF");
                return false;
            }
            printf("eth %s %s sock %u recvbuf_len %d\n", iface->name, iface->ifname, i, recvbuf_len);*/
        }
        eid->sockfd[i] = sock;
        eid->pcp_used[i] = 1;
    }

    iface->recvfd = eid->sockfd[0];
    iface->dropstat_cntr = 0;
    iface->dropstat_last_warn = 0;
    iface->state = IFS_OPEN;
    return true;
}

static bool eth_close(struct Interface *iface)
{
    struct EthIfData *eid = iface->iface_private;
    for (unsigned i=0; i<8; i++) {
        if (eid->pcp_used[i]) {
            close(eid->sockfd[i]);
        }
    }
    free(eid);
    return true;
}

static void eth_mac_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = state;
    struct EthIfData *eid = iface->iface_private;
    struct Value val = {&eid->mac, 0, 6*8};
    consumer(consumer_state, &val, p);
}


static value_producer *eth_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    (void)iface;
    // eth only has one property
    if (strcmp(property, "mac") != 0) {
        log_error("eth_get_property_reader unknown property '%s'", property);
        return NULL;
    }
    if (target_type != FT_MACADDRESS) {
        log_error("eth_get_property_reader target type %d is not mac address", target_type);
        return NULL;
    }
    if ((target->bitoffset % 8) || (target->bitcount != 6*8)) {
        log_error("eth_get_property_reader target position %u %u invalid",
                target->bitoffset, target->bitcount);
        return NULL;
    }
    return eth_mac_producer;
}

struct Interface *new_eth_interface(const char *name, const char *ifname)
{
    struct Interface *iface = calloc_struct(Interface);
    iface->name = strdup(name);
    iface->ifname = strdup(ifname);
    iface->type = IF_ETH;
    iface->state = IFS_INIT;
    iface->recv = eth_recv;
    iface->send = eth_send;
    iface->open = eth_open;
    iface->close_ = eth_close;
    iface->get_property_reader = eth_get_property_reader;

    struct EthIfData *eid = calloc_struct(EthIfData);
    iface->iface_private = eid;

    iface->parsetree = new_parsetree(iface);
    parsetree_ref(iface->parsetree);

    return iface;
}
