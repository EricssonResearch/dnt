// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "packet.h"
#include "time_utils.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include <sys/socket.h>
#include <arpa/inet.h> /* ntohs() */
#include <linux/net_tstamp.h> /* SOF_TIMESTAMPING_* */
//#include <linux/sockios.h> /* SIOCSHWTSTAMP */
#include <ifaddrs.h>
#include <netdb.h> /* getnameinfo() */
#include <linux/errqueue.h>
#include <linux/if_packet.h> /* PACKET_STATISTICS */

DEFAULT_LOGGING_MODULE(INTERFACE, WARNING);

#define PKT_DROP_WARNING_THRESHOLD 10
#define PKT_DROP_WARNING_INTERVAL_SEC 5

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
        log_perror("setsockopt SO_TIMESTAMPING");
    } else {
        if (0) printf("setsockopt SO_TIMESTAMPING success for '%s' on '%s'\n",
                sockname, ifname);
    }
}

static void get_rx_tstamp(struct msghdr *msg, struct Packet *p, void *userdata)
{
    (void)userdata;

    // process the cmsg to get the timestamp
    for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(msg); cmsg; cmsg=CMSG_NXTHDR(msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_SOCKET:
                if (cmsg->cmsg_type == SCM_TIMESTAMPING) {
                    struct timespec *tstamp;
                    if (cmsg->cmsg_len < sizeof(struct cmsghdr)+3*sizeof(struct timespec)) {
                        log_error("alignment problem cmsg_len %zu tstamp %zu cmsghdr %zu",
                                cmsg->cmsg_len, sizeof(struct timespec), sizeof(struct cmsghdr));
                    } else {
                        // we aligned msg.msg_control so the alignment should be okay here
                        tstamp = (struct timespec *)CMSG_DATA(cmsg);
                        //printf("RX SW %ld.%09ld HW %ld.%09ld\n",
                        //        tstamp[0].tv_sec, tstamp[0].tv_nsec,
                        //        tstamp[2].tv_sec, tstamp[2].tv_nsec);
                        p->recv_time = tstamp[0];
                        //TODO also set p->timestamp
                    }
                }
                break;
        }
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
        log_perror("recvmsg on %s", iface->name);
        return delete_packet(p);
    }

    if (packet_dummy(p)) {
        log_debug("packet overflow, received on interface %s", iface->name);
        return delete_packet(p);
    }
    if (iface->state != IFS_OPEN) {
        return delete_packet(p);
    }

    if (res > 0) {
        p->len = res;
    }

    get_rx_tstamp(&msg, p, userdata);

    timespec_to_tsntstamp(p->timestamp, &p->recv_time);

    if (msg_cb)
        msg_cb(&msg, p, userdata);

    log_packet("IFACE %s recv %u bytes", iface->name, p->len);
    return p;
}

static void dropstat(struct Interface *iface, int socket)
{
    if (iface->dropstat_cntr > 5000) {
        struct tpacket_stats stats;
        unsigned optlen = sizeof(stats);
        if (getsockopt(socket, SOL_PACKET, PACKET_STATISTICS, &stats, &optlen) < 0) {
            log_warning("eth %s send: can't get packet statistics", iface->name);
        } else {
            if (stats.tp_drops >= PKT_DROP_WARNING_THRESHOLD) {
                struct timespec now;
                clock_gettime(CLOCK_REALTIME, &now);
                if (now.tv_sec - iface->dropstat_last_warn >= PKT_DROP_WARNING_INTERVAL_SEC) {
                    log_warning("eth %s send: drop count %d", iface->name, stats.tp_drops);
                    iface->dropstat_last_warn = now.tv_sec;
                }
            }
        }
        iface->dropstat_cntr = 0;
    }
}

bool iface_common_send(struct Interface *iface, struct Packet *p, int socket, void *dst, unsigned dstlen)
{
    if (iface->state != IFS_OPEN) {
        log_warning("send on %s: interface is not open", iface->name);
        return false;
    }

    if (p->header_count < 1) {
        log_error("send on %s: packet doesn't have headers", iface->name);
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
        log_perror("sendmsg on %s", iface->name);
        return false;
    }

    packet_print(p);

    dropstat(iface, socket);

    return true;
}

struct MonitorState {
    const char *name;
    int socket;
    int family;
    struct Thread *thread;
};

static void *socket_monitor_thread(void *param)
{
    struct MonitorState *st = param;

    nfds_t nfds = 1;
    struct pollfd fds[1];
    fds[0].fd = st->socket;
    fds[0].events = POLLERR;

    while (1) {
        // MSG_ERRQUEUE is always non-blocking, we need to poll it first
        int poll_num = poll(fds, nfds, -1);
        if (poll_num == -1) {
            if (errno == EINTR)
                continue;
            log_perror("poll on sockerr for %s", st->name);
            continue;
        }
        if (poll_num == 0)
            continue;

        char data[1600]; //TODO we don't need this buffer at all
        char control[1000]
            __attribute__ ((aligned(__alignof__(struct cmsghdr))));
        struct msghdr msg;
        struct iovec iov;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = NULL; // we already know the source
        iov.iov_base = data;
        iov.iov_len = sizeof(data);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        int res = recvmsg(st->socket, &msg, MSG_ERRQUEUE);
        //printf("res %d controllen %zu\n", res, msg.msg_controllen);
        if (res < 0) {
            log_perror("recvmsg on sockerr for %s", st->name);
            continue;
        }

        for (struct cmsghdr *cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR) {
                struct sock_extended_err *serr = (void *) CMSG_DATA(cmsg);
                // serr->ee_origin = 2 (SO_EE_ORIGIN_ICMP)
                log_error("error on %s '%s' ICMP type %u code %u",
                        st->name, strerror(serr->ee_errno),
                        serr->ee_type, serr->ee_code);
            } else if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVERR) {
                struct sock_extended_err *serr = (void *) CMSG_DATA(cmsg);
                // serr->ee_origin = 3 (SO_EE_ORIGIN_ICMP6)
                log_error("error on %s '%s' ICMPv6 type %u code %u",
                        st->name, strerror(serr->ee_errno),
                        serr->ee_type, serr->ee_code);
            } else {
                log_error("unexpected error on %s level %u type %u",
                        st->name, cmsg->cmsg_level, cmsg->cmsg_type);
            }
        }
    }

    return NULL;
}

void *monitor_error_queue(int socket, int family, const char *name)
{
    if (family == AF_INET6) {
        int enable = 1;
        if (setsockopt(socket, IPPROTO_IPV6, IPV6_RECVERR, &enable, sizeof(enable)) < 0) {
            log_perror("setsockopt IPV6_RECVERR");
            return false;
        }
    } else if (family == AF_INET) {
        int enable = 1;
        if (setsockopt(socket, IPPROTO_IP, IP_RECVERR, &enable, sizeof(enable)) < 0) {
            log_perror("setsockopt IP_RECVERR");
            return false;
        }
    } else {
        log_error("cannot monitor error queue on socket family %d", family);
        return false;
    }

    struct MonitorState *st = calloc_struct(MonitorState);
    st->name = name;
    st->socket = socket;
    st->family = family;

    char thname[16];
    snprintf(thname, sizeof(thname), "sockmon %s", name);
    st->thread = thread_launch(thname, socket_monitor_thread, st);
    if (st->thread == NULL) {
        log_error("could not create error queue monitoring thread for %s", name);
        free(st);
        //TODO disable RECVERR?
        return NULL;
    }

    return st;
}

void stop_monitoring_error_queue(void *monitor)
{
    struct MonitorState *st = monitor;
    if (st) {
        thread_stop(st->thread);

        if (st->family == AF_INET6) {
            int enable = 0;
            if (setsockopt(st->socket, IPPROTO_IPV6, IPV6_RECVERR, &enable, sizeof(enable)) < 0) {
                log_perror("setsockopt IPV6_RECVERR");
            }
        } else if (st->family == AF_INET) {
            int enable = 0;
            if (setsockopt(st->socket, IPPROTO_IP, IP_RECVERR, &enable, sizeof(enable)) < 0) {
                log_perror("setsockopt IP_RECVERR");
            }
        }
    }
    free(st);
}

// Used for debugging, always print to stdout
void print_ifaddrs(struct ifaddrs *ifa)
{
    int family = ifa->ifa_addr->sa_family;
    printf("IFAddress of %s family %s ", ifa->ifa_name, (family == AF_PACKET) ? "AF_PACKET" :
            (family == AF_INET) ? "AF_INET" :
            (family == AF_INET6) ? "AF_INET6" : "unknown");
    if (family == AF_INET || family == AF_INET6) {
        char host[NI_MAXHOST];
        int err = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) :
                sizeof(struct sockaddr_in6),
                host, NI_MAXHOST,
                NULL, 0, NI_NUMERICHOST);

        if (err) {
            printf("getnameinfo() failed with %s\n", gai_strerror(err));
        } else {
            printf("%s\n", host);
        }
    } else {
        //TODO this is usually AF_PACKET
        printf("\n");
    }
}
