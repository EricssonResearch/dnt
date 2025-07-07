// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam_eth.h"
#include "if_utils.h"
#include "inet_utils.h"
#include "interface.h"
#include "log.h"
#include "oam.h"
#include "packet.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h> /* ntohs() */

#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h> /* struct ifreq */
#include <net/ethernet.h> /* struct ether_addr */

#include <linux/if_ether.h> /* ETH_P_ALL */
#include <linux/if_packet.h> /* struct sockaddr_ll, PACKET_AUXDATA TODO netpacket/packet.h? */
#include <linux/if_vlan.h> /* vlan ioctl()  */
#include <linux/sockios.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

struct OamIfData {
    enum ProtocolID type;
    int ifindex;
    int vlan;
    struct ether_addr mac;
    char oam_eth_str[ETHER_ADDSTRLEN];  // hold eth address in text format
    unsigned short uid; // unique id of this iface
};

unsigned short oam_eth_if_get_uid(const struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    return oid->uid;
}

char *oam_eth_if_get_mac(const struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    return oid->oam_eth_str;
}

unsigned oam_eth_if_get_vlan(const struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    return oid->vlan;
}


static bool oam_eth_recv(struct Interface *iface)
{
    char buffer[2000];
    int n;

    n = recv(iface->recvfd, buffer, sizeof(buffer)-1, 0);
    if (n>0) {
        buffer[n]=0;
        //dump_packet(buffer+14+4+3, n-22);               // just debug
        oam_receive_outofband(iface, &buffer[14+4+3]);
    }

    return true;
}

static bool oam_eth_send(struct Interface *iface, struct Packet *p)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;

    unsigned char *dst_mac = p->buf + p->headers[0].start;
    unsigned char *src_mac = dst_mac + 6;
    if( (src_mac[0]==0) && (src_mac[1]==0) && (src_mac[2]==0) &&
        (src_mac[3]==0) &&  (src_mac[4]==0) && (src_mac[5]==0)) {
        memcpy(src_mac, (char* )&oid->mac, 6);
    }

    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_ifindex = oid->ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, dst_mac, ETH_ALEN);

    return iface_common_send(iface, p, iface->recvfd, &socket_address, sizeof(socket_address));
}

static bool oam_eth_open(struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open OAM_ETH interface %s: already opened", iface->name);
        return false;
    }

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_CFM));
    if (sock < 0) {
        log_perror("oam_eth %s socket", iface->name);
        return false;
    }

    // maybe use if_nametoindex("eth0.100")?
    struct ifreq if_mac, if_idx;
    memset(&if_mac, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mac.ifr_name, iface->ifname, IFNAMSIZ-1);
    strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
    if (ioctl(sock, SIOCGIFHWADDR, &if_mac) < 0) {
        log_perror("SIOCGIFHWADDR");
        close(sock);
        return false;
    }
    if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
        log_perror("SIOCGIFINDEX");
        close(sock);
        return false;
    }
    oid->ifindex = if_idx.ifr_ifindex;
    oid->mac = *((struct ether_addr *)&if_mac.ifr_hwaddr.sa_data);
    ether_ntop(&oid->mac, oid->oam_eth_str, sizeof(oid->oam_eth_str));
    oid->uid = oid->mac.ether_addr_octet[5]+(oid->mac.ether_addr_octet[4]<<8);  // TODO: is this OK??
    //oid->uid = djb2_hash(oid->oam_eth_str);                                   // maybe hash?

    //      store this in an interface property

    struct vlan_ioctl_args vlan_args;
    memset(&vlan_args, 0, sizeof(vlan_args));
    strncpy(vlan_args.device1, iface->ifname, sizeof(vlan_args.device1) - 1);
    vlan_args.cmd = GET_VLAN_VID_CMD;

    if (ioctl(sock, SIOCGIFVLAN, &vlan_args) < 0)
        oid->vlan = -1;
    else
        oid->vlan = vlan_args.u.VID;

/*    int enable = 1;
    if (setsockopt(sock, SOL_PACKET, PACKET_AUXDATA, &enable, sizeof(enable)) < 0) {
        log_perror("setsockopt PACKET_AUXDATA");
        close(sock);
        return false;
    }
*/
    // bind to device
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_protocol = htons(ETH_P_CFM);
    socket_address.sll_ifindex = if_idx.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
        log_perror("bind sock to iface");
        close(sock);
        return false;
    }
    /* set iface to promiscuous mode
    struct packet_mreq mreq;
    mreq.mr_type = PACKET_MR_PROMISC;
    mreq.mr_ifindex = if_idx.ifr_ifindex;
    mreq.mr_alen = 0;
    memset(&mreq.mr_address, 0, sizeof(mreq.mr_address));
    if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        log_perror("setsockopt mreq promisc");
        close(sock);
        return false;
    }
    */
    // Ignore outgoing packets sent on other priority sockets (since Linux 4.20)
    int true_flag = 1;
    if (setsockopt(sock, SOL_PACKET, PACKET_IGNORE_OUTGOING, &true_flag, sizeof(true_flag)) < 0) {
        log_perror("setsockopt PACKET_IGNORE_OUTGOING");
        close(sock);
        return false;
    }

    log_info("OAM_ETH return interface %s %s (idx: %d) uid 0x%.4x", iface->name, iface->ifname, oid->ifindex, oid->uid);
    iface->recvfd = sock;
    iface->state = IFS_OPEN;
    add_oam_if(iface);
    return true;
}

static bool oam_eth_close(struct Interface *iface)
{
    struct OamIfData *oid = (struct OamIfData *)iface->iface_private;
    close(iface->recvfd);
    free(oid);
    log_info("OAM_ETH interface %s closed", iface->name);
    return true;
}

struct Interface *new_oam_eth_interface(const char *name, const char *ifname)
{
    _NEW_IFACE(IF_OAM_ETH);
    iface->recv = oam_eth_recv;
    iface->send = oam_eth_send;
    iface->open = oam_eth_open;
    iface->close_ = oam_eth_close;

    struct OamIfData *oid = calloc_struct(OamIfData);
    iface->iface_private = oid;
    iface->ifname = strdup(ifname);
    //note: oid->uid will be set in open()

    return iface;
}
