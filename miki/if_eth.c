
#include "if_eth.h"
#include "packet.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h> /* close() */
#include <sys/socket.h>
#include <netinet/in.h>

#include <linux/if_packet.h> /* struct sockaddr_ll, PACKET_AUXDATA TODO netpacket/packet.h? */

struct EthIfData {
    int sockfd[8];
    bool pcp_used[8];
    int mtu;
};

static bool eth_recv(struct Interface *iface, struct Packet *p)
{
    struct EthIfData *eid = iface->iface_private;
    (void)eid;

    //TODO all of this stuff was copied from detcloud/recv.c

    //char data[9100];
    char control[1000]
        __attribute__ ((aligned(__alignof__(struct cmsghdr))));
    struct msghdr msg;
    struct iovec iov;
    struct sockaddr_in from_addr;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &from_addr;
    iov.iov_base = p->buf + p->start;//data;
    iov.iov_len = PACKET_BUF_LEN - p->start;//sizeof(data);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    int res = recvmsg(iface->recvfd, &msg, 0);
    //printf("recvmsg %d controllen %zu\n", res, msg.msg_controllen);
    if (res < 0) {
        perror("recvmsg");
        return false;
    }

    if (res > 0) {
        p->len = res;
        //TODO what else?
    }

    //TODO process the cmsg to get the vlan header info
    //TODO if we have vlan, restore it in the packet
    //  packet_add_header(p, 1, vlan), set vlan_tci
    for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_PACKET:
                if (cmsg->cmsg_type == PACKET_AUXDATA) {
                    struct tpacket_auxdata *aux = (struct tpacket_auxdata *)CMSG_DATA(cmsg);
                    //TODO when is TP_STATUS_VLAN_VALID?
                    printf("AUX 0x%.08x len %u VLAN 0x%.04x EtherType 0x%.04x ",
                            aux->tp_status, aux->tp_len, aux->tp_vlan_tci, aux->tp_vlan_tpid);
                }
                break;
        }
    }

    return true;
}

static void eth_send(struct Interface *iface, struct Packet *p)
{
    struct EthIfData *eid = iface->iface_private;
    (void)p;
    (void)eid;
    //TODO the packet contains a full header structure description
    //      if it has a svlan or cvlan after the eth, read the pcp from it
    //      otherwise pcp = 0
    //TODO sendmsg() on eid->sockfg[pcp]
}

static bool eth_del(void *iface_private)
{
    struct EthIfData *eid = iface_private;
    for (unsigned i=0; i<8; i++) {
        if (eid->pcp_used[i]) {
            close(eid->sockfd[i]);
        }
    }

    return true;
}


bool init_eth_interface(struct Interface *iface, const char *name, const char *ifname)
{
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    iface->ifname = strdup(ifname);
    iface->type = IF_ETH;
    iface->recv = eth_recv;
    iface->send = eth_send;
    iface->del = eth_del;

    struct EthIfData *eid = calloc_struct(EthIfData);
    //TODO open sockets etc.
    eid->mtu = 1500; //TODO
    iface->iface_private = eid;
    iface->recvfd = eid->sockfd[0];

    return true;
}

