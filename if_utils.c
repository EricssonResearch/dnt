// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifdef _GNU_SOURCE /* stupid g++ implicitly defines this */
#undef _GNU_SOURCE /* we want the standard version of strerror_r */
#endif

#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "packet.h"
#include "parsetree.h"
#include "pipeline.h"
#include "time_utils.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>

#include <sys/timex.h> /* ntptimeval */
#include <sys/ioctl.h> /* ioctl */
#include <net/if.h> /* ifreq */
#include <sys/socket.h>
#include <arpa/inet.h> /* ntohs() */
#include <linux/net_tstamp.h> /* SOF_TIMESTAMPING_* */
#include <linux/sockios.h> /* SIOCSHWTSTAMP */
#include <ifaddrs.h>
#include <netdb.h> /* getnameinfo() */
#include <linux/errqueue.h>
#include <linux/if_packet.h> /* PACKET_STATISTICS */
#include <linux/version.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

#define PKT_DROP_WARNING_THRESHOLD 10
#define PKT_DROP_WARNING_INTERVAL_SEC 5

#ifndef SO_TXTIME
#define SO_TXTIME 61
#define SCM_TXTIME SO_TXTIME
#endif

const int filters[] =
{
    HWTSTAMP_FILTER_ALL,
    HWTSTAMP_FILTER_SOME,
    HWTSTAMP_FILTER_PTP_V2_EVENT, // ptp4l uses this
    HWTSTAMP_FILTER_PTP_V2_L2_EVENT, // ptp4l fall back on this
    HWTSTAMP_FILTER_NONE
};

const char* const filter_str[] =
{
	[HWTSTAMP_FILTER_NONE]                = "HWTSTAMP_FILTER_NONE",
	[HWTSTAMP_FILTER_ALL]                 = "HWTSTAMP_FILTER_ALL",
	[HWTSTAMP_FILTER_SOME]                = "HWTSTAMP_FILTER_SOME",
	[HWTSTAMP_FILTER_PTP_V1_L4_EVENT]     = "HWTSTAMP_FILTER_PTP_V1_L4_EVENT",
	[HWTSTAMP_FILTER_PTP_V1_L4_SYNC]      = "HWTSTAMP_FILTER_PTP_V1_L4_SYNC",
	[HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ] = "HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ",
	[HWTSTAMP_FILTER_PTP_V2_L4_EVENT]     = "HWTSTAMP_FILTER_PTP_V2_L4_EVENT",
	[HWTSTAMP_FILTER_PTP_V2_L4_SYNC]      = "HWTSTAMP_FILTER_PTP_V2_L4_SYNC",
	[HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ] = "HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ",
	[HWTSTAMP_FILTER_PTP_V2_L2_EVENT]     = "HWTSTAMP_FILTER_PTP_V2_L2_EVENT",
	[HWTSTAMP_FILTER_PTP_V2_L2_SYNC]      = "HWTSTAMP_FILTER_PTP_V2_L2_SYNC",
	[HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ] = "HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ",
	[HWTSTAMP_FILTER_PTP_V2_EVENT]        = "HWTSTAMP_FILTER_PTP_V2_EVENT",
	[HWTSTAMP_FILTER_PTP_V2_SYNC]         = "HWTSTAMP_FILTER_PTP_V2_SYNC",
	[HWTSTAMP_FILTER_PTP_V2_DELAY_REQ]    = "HWTSTAMP_FILTER_PTP_V2_DELAY_REQ",
	[HWTSTAMP_FILTER_NTP_ALL]             = "HWTSTAMP_FILTER_NTP_ALL"
};

void enable_rx_tstamp(int sock, const char *sockname,
        const char *ifname/*, enum hwtstamp_rx_filters filter*/)
{
    struct ifreq hwtstamp;
    struct hwtstamp_config hwconfig, hwconfig_req;
    memset(&hwtstamp, 0, sizeof(hwtstamp));
    strncpy(hwtstamp.ifr_name, ifname, sizeof(hwtstamp.ifr_name)-1);
    hwtstamp.ifr_data = (char *)&hwconfig;
    memset(&hwconfig, 0, sizeof(hwconfig));
    hwconfig.tx_type = HWTSTAMP_TX_OFF;

    // try RX filters that suitable for the hardware
    for (unsigned int i = 0; i < (sizeof(filters) / sizeof(filters[0])); i++) {
        hwconfig.rx_filter = filters[i];
        hwconfig_req = hwconfig;

        if (ioctl(sock, SIOCSHWTSTAMP, &hwtstamp) < 0) {
            if (errno == EINVAL || errno == ENOTSUP) {
                log_debug("SIOCSHWTSTAMP: HW timestamping for '%s' on '%s' is not available",
                        sockname, ifname);
                break;
            } else if (errno == ERANGE) {
                log_debug("SIOCSHWTSTAMP: The requested HW timestamping mode %s is not supported by the hardware.",
                        filter_str[hwconfig_req.rx_filter]);
            } else {
                log_perror("SIOCSHWTSTAMP:");
                break;
            }
        } else {
            log_debug("SIOCSHWTSTAMP: rx_filter requested %s got %s",
                    filter_str[hwconfig_req.rx_filter], filter_str[hwconfig.rx_filter]);
            break;
        }
    }

    int so_timestamping_flags =
        // generate these timestamps
        SOF_TIMESTAMPING_RX_HARDWARE |
        SOF_TIMESTAMPING_RX_SOFTWARE |
        // report these timestamps in cmsg
        SOF_TIMESTAMPING_RAW_HARDWARE |
        SOF_TIMESTAMPING_SOFTWARE |
        0;
    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
                &so_timestamping_flags, sizeof(so_timestamping_flags)) < 0) {
        log_perror("setsockopt SO_TIMESTAMPING");
    } else {
        if (0) log_info("setsockopt SO_TIMESTAMPING success for '%s' on '%s'",
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
                    if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct scm_timestamping))) {
                        log_error("alignment problem cmsg_len %zu tstamp %zu cmsghdr %zu",
                                cmsg->cmsg_len, sizeof(struct timespec), sizeof(struct cmsghdr));
                    } else {
                        // we aligned msg.msg_control so the alignment should be okay here
                        tstamp = (struct timespec *)CMSG_DATA(cmsg);
                        log_debug("RX SW %ld.%09ld HW %ld.%09ld",
                                tstamp[0].tv_sec, tstamp[0].tv_nsec,
                                tstamp[2].tv_sec, tstamp[2].tv_nsec);
                        if (tstamp[2].tv_sec == 0)
                            p->recv_time = tstamp[0]; // use SW timestamp
                        else
                            p->recv_time = tstamp[2]; // use HW timestamp
                        //TODO also set p->timestamp
                    }
                }
                break;
        }
    }
}

bool enable_so_txtime(int sock, const char *sockname, const char *ifname, bool deadline)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0)
    static struct sock_txtime txtime = { .clockid = CLOCK_TAI, .flags = SOF_TXTIME_REPORT_ERRORS };

    if (deadline)
        txtime.flags |= SOF_TXTIME_DEADLINE_MODE;

    if (setsockopt(sock, SOL_SOCKET, SO_TXTIME, &txtime, sizeof(txtime))) {
        log_perror("setsockopt SO_TXTIME failed on %s, %s", sockname, ifname);
        return false;
    }

    log_debug("SO_TXTIME enabled on '%s'", ifname);
    return true;
#else
    log_warning("No SO_TXTIME support (Linux version >= 4.19 required)");
    (void) sock;
    (void) sockname;
    (void) ifname;
    (void) deadline;
    return false;
#endif
}

struct Packet *iface_common_recv(struct Interface *iface, msghdr_process_cb *msg_cb, void *userdata)
{
    struct Packet *p = new_packet(iface);

    char control[1000]
        __attribute__ ((aligned(__alignof__(struct cmsghdr))));
    struct msghdr msg;
    struct iovec iov;
    memset(&msg, 0, sizeof(msg));
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
    } else {
        return delete_packet(p);
    }

    __atomic_add_fetch(&iface->recv_packets, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&iface->recv_octets, p->len, __ATOMIC_RELAXED);

    get_rx_tstamp(&msg, p, userdata);
    log_debug("Used timestamp: %ld.%09ld", p->recv_time.tv_sec, p->recv_time.tv_nsec);

    timespec_to_tsntstamp(p->timestamp, &p->recv_time);

    if (msg_cb)
        msg_cb(&msg, p, userdata);

    log_packet("IFACE %s recv %u bytes", iface->name, p->len);
    return p;
}

static void dropstat(struct Interface *iface, int socket)
{
    if (iface->dropstat_cntr++ > 5000) {
        struct tpacket_stats stats;
        unsigned optlen = sizeof(stats);
        if (getsockopt(socket, SOL_PACKET, PACKET_STATISTICS, &stats, &optlen) < 0) {
            log_warning_once("eth %s send: can't get packet statistics", iface->name);
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

// ntp_gettime is available since glibc 2.1 version but the ABI
// was different, since "struct ntptimeval" had no "tai" member
// after glibc 2.12 ntp_gettime uses ntp_gettimex which get TAI too
#if NTP_API >= 4
    char control[CMSG_SPACE(sizeof(uint64_t))];
    memset(control, 0, sizeof(control));
    struct cmsghdr *cm = NULL;
    struct timespec now_ts, txtime_ts, tstamp_ts;
    uint64_t txtime;

    if (p->offload && iface->delay_offload) {
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        // get the difference between CLOCK_TAI and CLOCK_REALTIME
        struct ntptimeval offset;
        memset(&offset, 0, sizeof(offset));
        ntp_gettime(&offset);

        // calculate the txtime with CLOCK_REALTIME and convert it to CLOCK_TAI
        clock_gettime(CLOCK_REALTIME, &now_ts);
        timespec_from_tsntstamp(&tstamp_ts, p->timestamp, &now_ts);
        timespecadd(&tstamp_ts, &p->delay, &txtime_ts);
        txtime = (txtime_ts.tv_sec + offset.tai) * NSEC_PER_SEC + txtime_ts.tv_nsec;
        log_debug("txtime: %lu", txtime);

        // set the txtime in a control message
        cm = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_TXTIME;
        cm->cmsg_len = CMSG_LEN(sizeof(txtime));
        memcpy(CMSG_DATA(cm), &txtime, sizeof(txtime));
    }
#endif

    packet_logcat(p, "%s ", iface->name);

    __atomic_add_fetch(&iface->send_packets, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&iface->send_octets, packet_length(p), __ATOMIC_RELAXED);

    if (sendmsg(socket, &msg, 0) < 0) {
        // rate limit the error reports
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (time_diff_us(now, iface->last_alert) > 1000*1000*5) {
            log_perror("sendmsg on %s", iface->name);
            struct JsonValue *js = json_object();
            json_object_insert(js, "interface", json_string(iface->name));
            char err[2048];
            strerror_r(errno, err, sizeof(err));
            json_object_insert(js, "error", json_string(err));
            notification_push_event("send", NOTIF_ERROR, js);
            iface->last_alert = now;
        }
        return false;
    }

    dropstat(iface, socket);

    return true;
}

bool iface_common_process(struct Interface *iface, struct Packet *p)
{
    if (iface->parsetree_ == NULL) {
        iface->parsetree_ = new_parsetree(iface);
    }

    struct PipelineIterator *pi = parsetree_identify(iface->parsetree_, p);
    if (pi == NULL) {
        delete_packet(p);
        return false;
    } else {
        // the iterator owns the packet
        // the iterator deletes itself when it's done with the processing
        pipe_iterator_run(pi);
        return true;
    }
}


struct MonitorState {
    const char *name;
    int socket;
    int family;
    struct Thread *thread;
};

static void *socket_monitor_thread(void *param)
{
    struct MonitorState *st = (struct MonitorState *)param;

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
                struct sock_extended_err *serr = (struct sock_extended_err *) CMSG_DATA(cmsg);
                char errstr[1024] = {0}; // initialize because strerror_r() can fail
                strerror_r(serr->ee_errno, errstr, sizeof(errstr));
                // serr->ee_origin = 2 (SO_EE_ORIGIN_ICMP)
                log_warning("got ICMP error message on %s '%s' (type %u code %u)",
                        st->name, errstr,
                        serr->ee_type, serr->ee_code);
            } else if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVERR) {
                struct sock_extended_err *serr = (struct sock_extended_err *) CMSG_DATA(cmsg);
                char errstr[1024] = {0}; // initialize because strerror_r() can fail
                strerror_r(serr->ee_errno, errstr, sizeof(errstr));
                // serr->ee_origin = 3 (SO_EE_ORIGIN_ICMP6)
                log_warning("got ICMPv6 error message on %s '%s' (type %u code %u)",
                        st->name, errstr,
                        serr->ee_type, serr->ee_code);
            } else {
                log_warning("got unexpected error on %s level %u type %u",
                        st->name, cmsg->cmsg_level, cmsg->cmsg_type);
            }
        }
    }

    return NULL;
}

struct MonitorState *monitor_error_queue(int socket, int family, const char *name)
{
    if (family == AF_INET6) {
        int enable = 1;
        if (setsockopt(socket, IPPROTO_IPV6, IPV6_RECVERR, &enable, sizeof(enable)) < 0) {
            log_perror("setsockopt IPV6_RECVERR");
            return NULL;
        }
    } else if (family == AF_INET) {
        int enable = 1;
        if (setsockopt(socket, IPPROTO_IP, IP_RECVERR, &enable, sizeof(enable)) < 0) {
            log_perror("setsockopt IP_RECVERR");
            return NULL;
        }
    } else {
        log_error("cannot monitor error queue on socket family %d", family);
        return NULL;
    }

    struct MonitorState *st = calloc_struct(MonitorState);
    st->name = name;
    st->socket = socket;
    st->family = family;

    st->thread = thread_launch(socket_monitor_thread, st, "sockmon %s", name);
    if (st->thread == NULL) {
        log_error("could not create error queue monitoring thread for %s", name);
        free(st);
        //TODO disable RECVERR?
        return NULL;
    }

    return st;
}

struct MonitorState *stop_monitoring_error_queue(struct MonitorState *st)
{
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
    return NULL;
}

NotificationLevel iface_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct Interface *iface = (struct Interface *)self;

    struct JsonValue *ret = json_object();
    json_object_insert(ret, "recv_packets", json_number(iface->recv_packets));
    json_object_insert(ret, "recv_octets", json_number(iface->recv_octets));
    json_object_insert(ret, "send_packets", json_number(iface->send_packets));
    json_object_insert(ret, "send_octets", json_number(iface->send_octets));

    *msg = ret;
    return NOTIF_PULL;
}

void print_ifaddrs(struct ifaddrs *ifa)
{
    int family = ifa->ifa_addr->sa_family;
    log_debug("IFAddress of %s family %s ", ifa->ifa_name, (family == AF_PACKET) ? "AF_PACKET" :
            (family == AF_INET) ? "AF_INET" :
            (family == AF_INET6) ? "AF_INET6" : "unknown");
    if (family == AF_INET || family == AF_INET6) {
        char host[NI_MAXHOST];
        int err = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) :
                sizeof(struct sockaddr_in6),
                host, NI_MAXHOST,
                NULL, 0, NI_NUMERICHOST);

        if (err) {
            log_error("getnameinfo() failed with %s", gai_strerror(err));
        } else {
            log_debug("host %s", host);
        }
    } else {
        //TODO this is usually AF_PACKET
        //log_debug("");
    }
}

/* hex dump packet - just debug  */
void dump_packet(char *buffer, int n)
{
    char dump_str[4000], ch[8];
    bzero(ch, sizeof(ch));
    sprintf(dump_str,"Packet received %d bytes:\n", n);

    unsigned char *pp=(unsigned char *)buffer;
    for(int i=1; i<=128; i++){
        sprintf(ch, " %02x", *pp);
        strcat(dump_str, ch);
        pp++;
        if(i%16==0)
            strcat(dump_str, "\n");
    }
    log_debug("%s", dump_str);
}
