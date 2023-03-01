
#include "if_eth.h"
#include "packet.h"
#include "protocol.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h> /* close() */
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h> /* struct ifreq */

#include <linux/if_ether.h> /* ETH_P_ALL */
#include <linux/if_packet.h> /* struct sockaddr_ll, PACKET_AUXDATA TODO netpacket/packet.h? */

struct EthIfData {
    int ifindex;
    int sockfd[8];
    bool pcp_used[8]; //TODO we can't filter this
    int mtu;
};

static struct Packet *eth_recv(struct Interface *iface)
{
    struct EthIfData *eid = iface->iface_private;
    (void)eid;

    struct Packet *p = new_packet(iface);

    char control[1000]
        __attribute__ ((aligned(__alignof__(struct cmsghdr))));
    struct msghdr msg;
    struct iovec iov;
    struct sockaddr_in from_addr;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &from_addr;
    iov.iov_base = p->buf + p->start;
    iov.iov_len = PACKET_BUF_LEN - p->start;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    int res = recvmsg(iface->recvfd, &msg, 0);
    //printf("recvmsg %d controllen %zu\n", res, msg.msg_controllen);
    if (res < 0) {
        perror("recvmsg");
        return NULL;
    }

    if (packet_dummy(p)) {
        fprintf(stderr, "packet overflow, received on interface %s\n", iface->name);
        return delete_packet(p);
    }
    if (iface->shutdown) {
        return delete_packet(p);
    }

    //TODO ask for kernel RX timestamping and use that
    //  ask for both HW and SW, if HW==0 use SW (?)
    clock_gettime(CLOCK_REALTIME, &p->recv_time);

    if (res > 0) {
        p->len = res;
        //TODO what else?
    }
    printf("eth %s recv %u\n", iface->name, p->len);

    // process the cmsg to get the vlan header info
    // if we have vlan, restore it in the packet
    for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_PACKET:
                if (cmsg->cmsg_type == PACKET_AUXDATA) {
                    struct tpacket_auxdata *aux = (struct tpacket_auxdata *)CMSG_DATA(cmsg);
                    printf("AUX 0x%.08x len %u VLAN 0x%.04x EtherType 0x%.04x\n",
                            aux->tp_status, aux->tp_len, aux->tp_vlan_tci, aux->tp_vlan_tpid);

                    if (aux->tp_status & TP_STATUS_VLAN_VALID) {
                        printf("restoring vlan header in the packet\n");
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

    return p;
}

static bool eth_send(struct Interface *iface, struct Packet *p)
{
    struct EthIfData *eid = iface->iface_private;

    if (p->header_count < 1) {
        fprintf(stderr, "eth send: packet doesn't have headers\n");
        return false;
    }
    if (p->headers[0].type != PROTO_ID_ETH) {
        fprintf(stderr, "eth send: first header of the packet is not eth\n");
        return false;
    }

    unsigned char *dst_mac = p->buf + p->headers[0].start; //TODO use the protocol definition...
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_ifindex = eid->ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, dst_mac, ETH_ALEN);

    unsigned pcp = 0;
    //TODO if p->header_count > 1 && p->headers[1].type is cvlan or svlan)
    //          get pcp from that header

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &socket_address;
    msg.msg_namelen = sizeof(socket_address);

    //TODO optimization: merge headers if h[i+1].start = h[i].start+h[i].len
    //TODO struct iovec iov[p->header_count] if this compiles
    struct iovec *iov = calloc_struct_array(iovec, p->header_count);
    for (unsigned i=0; i<p->header_count; i++) {
        iov[i].iov_base = p->buf + p->headers[i].start;
        iov[i].iov_len = p->headers[i].len;
    }
    msg.msg_iov = iov;
    msg.msg_iovlen = p->header_count;

    if (sendmsg(eid->sockfd[pcp], &msg, 0) < 0) {
        perror("eth sendmsg");
        free(iov);
        return false;
    }

    free(iov);
    return true;
}

static bool eth_open(struct Interface *iface)
{
    struct EthIfData *eid = iface->iface_private;

    if (eid->sockfd[0] > 0) {
        fprintf(stderr, "open eth interface %s: already opened\n", iface->name);
        return false;
    }
    if (iface->parsetree == NULL) {
        fprintf(stderr, "open eth interface %s: no parsetree, expect trouble\n", iface->name);
        //TODO fatal?
    }

    //TODO only need 1 socket if using the eBPF prioritizing filter
    for (unsigned i=0; i<8; i++) {
        int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (sock < 0) {
            perror("socket");
            return false; //TODO cleanup on error
        }

        //TODO this only works for L4 sockets
        /*if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
            perror("setsockopt SO_BINDTODEVICE");
            return false; //TODO cleanup on error
        }*/
        if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &i, sizeof(i)) < 0) {
            perror("setsockopt SO_PRIORITY");
            return false; //TODO cleanup on error
        }

        if (i == 0) {
            struct ifreq if_mtu, if_mac, if_idx;
            memset(&if_mtu, 0, sizeof(struct ifreq));
            memset(&if_mac, 0, sizeof(struct ifreq));
            memset(&if_idx, 0, sizeof(struct ifreq));
            strncpy(if_mtu.ifr_name, iface->ifname, strlen(iface->ifname));
            strncpy(if_mac.ifr_name, iface->ifname, strlen(iface->ifname));
            strncpy(if_idx.ifr_name, iface->ifname, strlen(iface->ifname));
            if (ioctl(sock, SIOCGIFMTU, &if_mtu) < 0) {
                perror("SIOCGIFMTU");
                return false;
            }
            if (ioctl(sock, SIOCGIFHWADDR, &if_mac) < 0) {
                perror("SIOCGIFHWADDR");
                return false;
            }
            if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
                perror("SIOCGIFINDEX");
                return false;
            }
            eid->mtu = if_mtu.ifr_mtu;
            eid->ifindex = if_idx.ifr_ifindex;
            //TODO struct ether_addr src_mac = *((struct ether_addr *)&if_mac.ifr_hwaddr.sa_data);
            //      store this in an interface property

            int enable = 1;
            if (setsockopt(sock, SOL_PACKET, PACKET_AUXDATA, &enable, sizeof(enable)) < 0) {
                perror("setsockopt PACKET_AUXDATA");
                return false;
            }

            // bind to device
            struct sockaddr_ll socket_address = {0};
            socket_address.sll_family = AF_PACKET;
            socket_address.sll_protocol = htons(ETH_P_ALL);
            socket_address.sll_ifindex = if_idx.ifr_ifindex;
            if (bind(sock, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
                perror("bind sock to iface");
                return false;
            }
            // set iface to promiscuous mode
            struct packet_mreq mreq;
            mreq.mr_type = PACKET_MR_PROMISC;
            mreq.mr_ifindex = if_idx.ifr_ifindex;
            mreq.mr_alen = 0;
            memset(&mreq.mr_address, 0, sizeof(mreq.mr_address));
            if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
                perror("setsockopt mreq promisc");
                return false;
            }

            //TODO the old code also installs a BPF filter:
            //  Block all PACKET_OUTGOING packets sent on other priority sockets
            //  do we need that?
        } else {
            // set socket receive buffer to minimum (setting to zero will set it to minimum)
            int recvbuf_len = 0;
            socklen_t sl = sizeof(recvbuf_len);
            if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recvbuf_len, sl) < 0) {
                perror("setsockopt SO_RCVBUF");
                return false;
            }
            /*if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recvbuf_len, &sl) < 0) {
                perror("getsockopt SO_RCVBUF");
                return false;
            }
            printf("eth %s %s sock %u recvbuf_len %d\n", iface->name, iface->ifname, i, recvbuf_len);*/
        }
        eid->sockfd[i] = sock;
        eid->pcp_used[i] = 1;
    }

    iface->recvfd = eid->sockfd[0];
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

    return true;
}


bool init_eth_interface(struct Interface *iface, const char *name, const char *ifname)
{
    printf("init_eth_interface %s %s\n", name, ifname);
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    iface->ifname = strdup(ifname);
    iface->type = IF_ETH;
    iface->recv = eth_recv;
    iface->send = eth_send;
    iface->open = eth_open;
    iface->close_ = eth_close;

    struct EthIfData *eid = calloc_struct(EthIfData);
    iface->iface_private = eid;

    return true;
}

