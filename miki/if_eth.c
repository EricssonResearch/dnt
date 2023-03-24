
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
#include <linux/net_tstamp.h> /* SOF_TIMESTAMPING_* */
//#include <linux/sockios.h> /* SIOCSHWTSTAMP */
#include <linux/filter.h> /* eBPF */

struct EthIfData {
    int ifindex;
    int sockfd[8];
    bool pcp_used[8]; // indicate whether we have a socket open for this priority
    int mtu;
};

// copied from the code of the other TSN project
static void enable_rx_tstamp(int sock, const char *sockname,
        const char *ifname/*, enum hwtstamp_rx_filters filter*/)
{
/*    struct ifreq hwtstamp;
    struct hwtstamp_config hwconfig, hwconfig_req;
    memset(&hwtstamp, 0, sizeof(hwtstamp));
    strncpy(hwtstamp.ifr_name, ifname, sizeof(hwtstamp.ifr_name)-1);
    hwtstamp.ifr_data = (void *)&hwconfig;
    memset(&hwconfig, 0, sizeof(hwconfig));
    hwconfig.tx_type = HWTSTAMP_TX_OFF;
    hwconfig.rx_filter = filter;
    hwconfig_req = hwconfig;
    if (ioctl(sock, SIOCSHWTSTAMP, &hwtstamp) < 0) {
        if (errno == EINVAL || errno == ENOTSUP) {
            printf("SIOCSHWTSTAMP: HW timestamping for '%s' on '%s' is not available\n",
                    sockname, ifname);
        } else {
            perror("SIOCSHWTSTAMP");
        }
    } else {
        printf("SIOCSHWTSTAMP: tx_type requested %d got %d, rx_filter requested %d got %d\n",
                hwconfig_req.tx_type, hwconfig.tx_type,
                hwconfig_req.rx_filter, hwconfig.rx_filter);
    }*/

    int so_timestamping_flags =
        // generate these timestamps
    //    SOF_TIMESTAMPING_RX_HARDWARE |
        SOF_TIMESTAMPING_RX_SOFTWARE |
        // report these timestamps in cmsg
    //    SOF_TIMESTAMPING_RAW_HARDWARE |
        SOF_TIMESTAMPING_SOFTWARE |
        0;
    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
                &so_timestamping_flags, sizeof(so_timestamping_flags)) < 0) {
        perror("setsockopt SO_TIMESTAMPING");
    } else {
        printf("setsockopt SO_TIMESTAMPING success for '%s' on '%s'\n",
                sockname, ifname);
    }
}

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
        return delete_packet(p);
    }

    if (packet_dummy(p)) {
        fprintf(stderr, "packet overflow, received on interface %s\n", iface->name);
        return delete_packet(p);
    }
    if (iface->shutdown) {
        return delete_packet(p);
    }

    if (res > 0) {
        p->len = res;
    }
    printf("eth %s recv %u\n", iface->name, p->len);

    // process the cmsg to get the vlan header info and timestamp
    // if we have vlan, restore it in the packet
    for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_SOCKET:
                if (cmsg->cmsg_type == SCM_TIMESTAMPING) {
                    struct timespec *tstamp;
                    if (cmsg->cmsg_len < sizeof(struct cmsghdr)+3*sizeof(struct timespec)) {
                        fprintf(stderr, "cmsg_len %zu tstamp %zu cmsghdr %zu\n",
                                cmsg->cmsg_len, sizeof(struct timespec), sizeof(struct cmsghdr));
                    } else {
                        // we aligned msg.msg_control so the alignment should be okay here
                        tstamp = (struct timespec *)CMSG_DATA(cmsg);
                        printf("RX SW %ld.%09ld HW %ld.%09ld\n",
                                tstamp[0].tv_sec, tstamp[0].tv_nsec,
                                tstamp[2].tv_sec, tstamp[2].tv_nsec);
                        p->recv_time = tstamp[0];
                    }
                }
                break;
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

    uint16_t *p_vlan = (uint16_t*)(p->buf + p->start + 2*6);
    unsigned short ethertype = ntohs(*p_vlan);
    if (ethertype != ETH_P_8021Q && ethertype != ETH_P_8021AD) {
        printf("adding default cvlan to untagged packet\n");
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
        fprintf(stderr, "eth %s send: packet doesn't have headers\n", iface->name);
        return false;
    }
    if (p->headers[0].type != PROTO_ID_ETH) {
        fprintf(stderr, "eth %s send: first header of the packet is not eth\n", iface->name);
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
    if (p->header_count > 1) {
        if (p->headers[1].type == PROTO_ID_CVLAN || p->headers[1].type == PROTO_ID_SVLAN) {
            unsigned char *vlan = p->buf + p->headers[1].start;
            pcp = (*vlan) >> 5;
        }
    }
    printf("eth %s sending with priority %u\n", iface->name, pcp);

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &socket_address;
    msg.msg_namelen = sizeof(socket_address);

    //TODO optimization: merge headers if h[i+1].start = h[i].start+h[i].len
    struct iovec iov[PACKET_MAX_HEADER_NUM];
    for (unsigned i=0; i<p->header_count; i++) {
        iov[i].iov_base = p->buf + p->headers[i].start;
        iov[i].iov_len = p->headers[i].len;
    }
    msg.msg_iov = iov;
    msg.msg_iovlen = p->header_count;

    if (sendmsg(eid->sockfd[pcp], &msg, 0) < 0) {
        perror("eth sendmsg");
        return false;
    }

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
            close(sock);
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

            enable_rx_tstamp(sock, "eth rx", iface->ifname/*, HWTSTAMP_FILTER_ALL*/);

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
                perror("setsockopt attach eBPF filter");
                return false;
            }
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
    free(eid);
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

