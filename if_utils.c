
#include "if_utils.h"
#include "interface.h"
#include "packet.h"

#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <arpa/inet.h> /* ntohs() */
#include <linux/net_tstamp.h> /* SOF_TIMESTAMPING_* */
//#include <linux/sockios.h> /* SIOCSHWTSTAMP */

// copied from the code of the other TSN project
void enable_rx_tstamp(int sock, const char *sockname,
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

struct Packet *iface_common_recv(struct Interface *iface, msghdr_process_cb *msg_cb, void *userdata)
{
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
    if (iface->state != IFS_OPEN) {
        return delete_packet(p);
    }

    if (res > 0) {
        p->len = res;
    }

    if (msg_cb)
        msg_cb(&msg, p, userdata);

    return p;
}

bool iface_common_send(struct Interface *iface, struct Packet *p, int socket, void *dst, unsigned dstlen)
{
    if (iface->state != IFS_OPEN) {
        fprintf(stderr, "send on %s: interface is not open\n", iface->name);
        return false;
    }

    if (p->header_count < 1) {
        fprintf(stderr, "send on %s: packet doesn't have headers\n", iface->name);
        return false;
    }

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = dst;
    msg.msg_namelen = dstlen;

    //TODO optimization: merge headers if h[i+1].start = h[i].start+h[i].len
    struct iovec iov[PACKET_MAX_HEADER_NUM];
    for (unsigned i=0; i<p->header_count; i++) {
        iov[i].iov_base = p->buf + p->headers[i].start;
        iov[i].iov_len = p->headers[i].len;
    }
    msg.msg_iov = iov;
    msg.msg_iovlen = p->header_count;

    if (sendmsg(socket, &msg, 0) < 0) {
        perror("sendmsg");
        return false;
    }

    return true;
}

