// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_udp_in.h"
#include "conf_utils.h" /* TODO don't include conf_xxx.h here */
#include "if_oam.h"
#include "if_utils.h"
#include "inet_utils.h"
#include "interface.h"
#include "json.h"
#include "log.h"
#include "packet.h"
#include "parsetree.h"
#include "thread_utils.h"
#include "time_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>
#include <linux/rtnetlink.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

struct SenderList {
    unsigned port;
    int family;
    char *ip_str;
    struct sockaddr_storage addr;
    unsigned addrlen;
    char *ifname;
    struct SenderList *next;
};

struct UdpInIfData {
    unsigned ifindex;
    int mtu;
    unsigned port;
    int family;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } srcip;
    struct Thread *ifmon;
    struct SenderList *senders;
};

#define HAVE_IP (memcmp(&uid->srcip, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", sizeof(uid->srcip)) != 0)

static struct SenderList *delete_senderlist(struct SenderList *sl)
{
    while (sl) {
        struct SenderList *d = sl;
        sl = sl->next;
        free(d->ip_str);
        free(d->ifname);
        free(d);
    }
    return NULL;
}

static struct rtattr *find_rtattr(struct rtattr *attr, unsigned len, unsigned type)
{
    while (RTA_OK(attr, len)) {
        if (attr->rta_type == type) {
            return attr;
        }
        attr = RTA_NEXT(attr, len);
    }
    return NULL;
}

// uid->srcip is only assigned when a good one is found
// @returns false on fatal error
static bool set_srcip(const char *ifname, struct UdpInIfData *uid)
{
    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) < 0) {
        log_perror("udp-in getifaddrs");
        return false;
    }

    for (struct ifaddrs *ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != uid->family) continue;
        if (strcmp(ifa->ifa_name, ifname) != 0) continue;
        //print_ifaddrs(ifa);

        if (family == AF_INET6) {
            struct in6_addr *a6 = &((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
            if (IN6_IS_ADDR_LINKLOCAL(a6)) continue;
            uid->srcip.v6 = *a6;
            break;
        } else if (family == AF_INET) {
            uid->srcip.v4 = ((struct sockaddr_in*)(ifa->ifa_addr))->sin_addr;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return true;
}

static void send_notification(struct UdpInIfData *uid)
{
    char ip_str[INET6_ADDRSTRLEN];
    if (HAVE_IP) {
        if (inet_ntop(uid->family, &uid->srcip, ip_str, sizeof(ip_str)) == NULL) {
            log_error("%s failed to decode ip", thread_getname(uid->ifmon));
        }
    } else {
        strcpy(ip_str, "<none>");
    }

    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("newaddress"));
    json_object_insert(js, "code", json_string("notify"));
    struct JsonValue *ja = json_object();
    json_object_insert(ja, "ip", json_string(ip_str));
    json_object_insert(ja, "port", json_number(uid->port));
    json_object_insert(js, "address", ja);
    //TODO more info?

    int sock4 = -1;
    int sock6 = -1;

    for (struct SenderList *s=uid->senders; s; s=s->next) {
        int sock;
        if (s->family == AF_INET6) {
            if (sock6 < 0) {
                sock6 = socket(AF_INET6, SOCK_DGRAM, 0);
                if (sock6 < 0) {
                    log_perror("address notification create sock6");
                    goto out;
                }
            }
            sock = sock6;
        } else {
            if (sock4 < 0) {
                sock4 = socket(AF_INET, SOCK_DGRAM, 0);
                if (sock4 < 0) {
                    log_perror("address notification create sock4");
                    goto out;
                }
            }
            sock = sock4;
        }

        json_object_insert(js, "sendiface", json_string(s->ifname));
        unsigned js_length;
        char *js_string;
        js_string = json_serialize(js, &js_length);
        log_debug("notify sender %s port %u iface %s json '%s'", s->ip_str, s->port, s->ifname, js_string);

        int err = sendto(sock, js_string, js_length, 0, (struct sockaddr*)&s->addr, s->addrlen);
        if (err < 0) {
            log_pwarning("address notification sendto %s port %u", s->ip_str, s->port);
        }
        free(js_string);
    }

out:
    json_delete(js);
    //TODO have permanent sockets?
    if (sock4 >= 0) close(sock4);
    if (sock6 >= 0) close(sock6);
}

static void *iface_address_monitoring(void *arg)
{
    struct Interface *iface = (struct Interface *)arg;
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;

    while (uid->ifmon == NULL) {
        usleep(1000);
    }

    log_info("iface address monitoring %s ifname %s ifindex %d", iface->name, iface->ifname, uid->ifindex);

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        log_perror("%s could not create netlink socket", thread_getname(uid->ifmon));
        struct Thread *self = uid->ifmon;
        uid->ifmon = NULL;
        thread_exit(self);
    }

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));

    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    // nl_pid can be safely left as 0

    struct timeval timeout = {10, 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        log_perror("%s setsockopt timeout", thread_getname(uid->ifmon));
        close(sock);
        struct Thread *self = uid->ifmon;
        uid->ifmon = NULL;
        thread_exit(self);
    }

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_perror("%s could not bind netlink socket", thread_getname(uid->ifmon));
        close(sock);
        struct Thread *self = uid->ifmon;
        uid->ifmon = NULL;
        thread_exit(self);
    }

    struct timespec last_notify;
    send_notification(uid);
    clock_gettime(CLOCK_REALTIME, &last_notify);

    char recvbuf[8192]
        __attribute__ ((aligned(__alignof__(struct nlmsghdr))));

    while (1) {
        int err = recv(sock, recvbuf, sizeof(recvbuf), 0);

        if (err < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                log_debug("%s timeout", thread_getname(uid->ifmon));
                send_notification(uid);
                clock_gettime(CLOCK_REALTIME, &last_notify);
            } else {
                log_perror("%s recvmsg", thread_getname(uid->ifmon));
            }
            continue;
        }
        if (err == 0) continue;
        unsigned recvlen = err;

        log_debug("%s received %u bytes", thread_getname(uid->ifmon), recvlen);

        bool must_notify = false;
        for (struct nlmsghdr *nh=(struct nlmsghdr *)recvbuf; NLMSG_OK(nh, recvlen); nh=NLMSG_NEXT(nh, recvlen)) {
            if (nh->nlmsg_type == NLMSG_DONE) {
                log_debug("NLMSG_DONE\n"); // we get this in multipart reply (=never)
                break;
            } else if (nh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *nlerr = (struct nlmsgerr *)NLMSG_DATA(nh);
                log_error("%s RTNETLINK ERROR %s\n", thread_getname(uid->ifmon), strerror(-nlerr->error));
            } else if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK) {
                //TODO handle link down event: notify senders about the outage?
                //  note that on link down the addresses may also disappear, should not send duplicate notifications
            } else if (nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
                struct ifaddrmsg *ifa = (struct ifaddrmsg*)NLMSG_DATA(nh);
                if (ifa->ifa_index != uid->ifindex) continue;
                if (ifa->ifa_family != uid->family) continue;

                int flags = ifa->ifa_flags;
                struct rtattr *flags_attr = find_rtattr(IFA_RTA(ifa), nh->nlmsg_len, IFA_FLAGS);
                if (flags_attr) {
                    flags = ((int*)RTA_DATA(flags_attr))[0];
                }

                if (flags & IFA_F_TENTATIVE) continue;
                //if (flags & IFA_F_MANAGETEMPADDR) continue;

                struct rtattr *addr_attr = find_rtattr(IFA_RTA(ifa), nh->nlmsg_len, IFA_ADDRESS);
                if (addr_attr == NULL) {
                    log_error("%s netlink did not supply address!?!?", thread_getname(uid->ifmon));
                    continue;
                }

                unsigned addrlen = ifa->ifa_family == AF_INET6 ? 16 : 4; //TODO RTA_PAYLOAD(addr_attr)
                if (nh->nlmsg_type == RTM_NEWADDR) {
                    if (HAVE_IP) continue;

                    memcpy(&uid->srcip, RTA_DATA(addr_attr), addrlen);
                    must_notify = true;
                } else {
                    // check if the removed address is the one we are using
                    if (memcmp(&uid->srcip, RTA_DATA(addr_attr), addrlen) == 0) {
                        memset(&uid->srcip, 0, sizeof(uid->srcip));
                        // check if there is another address we can use instead
                        if (!set_srcip(iface->ifname, uid)) {
                            //TODO wtf?
                        }
                        must_notify = true;
                    }
                }
            }
        }

        if (must_notify) {
            char ip_str[INET6_ADDRSTRLEN];
            if (HAVE_IP) {
                if (inet_ntop(uid->family, &uid->srcip, ip_str, sizeof(ip_str)) == NULL) {
                    log_error("%s failed to decode ip", thread_getname(uid->ifmon));
                }
            } else {
                strcpy(ip_str, "<none>");
            }
            log_info("udp-in %s new ip %s port %u", iface->name, ip_str, uid->port);

            struct JsonValue *js = json_object();
            json_object_insert(js, "interface", json_string(iface->name));
            json_object_insert(js, "ip", json_string(ip_str));
            json_object_insert(js, "port", json_number(uid->port));
            notification_push_event("new src", NOTIF_INFO, js);
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        struct timespec diff;
        timespecsub(&now, &last_notify, &diff);
        if (diff.tv_sec > timeout.tv_sec) {
            must_notify = true;
        }

        if (must_notify) {
            send_notification(uid);
            clock_gettime(CLOCK_REALTIME, &last_notify);
        }
    }

    return NULL;
}

struct SenderState {
    struct SenderList *senders;
    struct SenderList *current;
};

static bool process_sender(char *str, void *userdata)
{
    struct SenderState *sest = (struct SenderState *)userdata;

    if (sest->current) {
        sest->current->ifname = strdup(str);
        sest->current->next = sest->senders;
        sest->senders = sest->current;
        sest->current = NULL;
    } else {
        char *ip_str = NULL;
        unsigned port = OAM_PORT;
        if (!parse_ip_port(str, &ip_str, &port)) {
            log_error("udp-in could not parse '%s' as ip and port", str);
            return false;
        }

        struct in_addr dst4;
        struct in6_addr dst6;
        int family; //TODO it would be useful if parse_ip_port also returned the family

        if (inet_pton(AF_INET, ip_str, &dst4) == 1) {
            family = AF_INET;
        } else if (inet_pton(AF_INET6, ip_str, &dst6) == 1) {
            family = AF_INET6;
        } else {
            //TODO how did parse_ip_port accept this??
            return false;
        }

        struct SenderList *current = calloc_struct(SenderList);
        current->port = port;
        current->family = family;
        current->ip_str = ip_str;

        if (family == AF_INET6) {
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&current->addr;
            a6->sin6_family = AF_INET6;
            a6->sin6_addr = dst6;
            a6->sin6_port = htons(port);
            current->addrlen = sizeof(struct sockaddr_in6);
        } else {
            struct sockaddr_in *a4 = (struct sockaddr_in *)&current->addr;
            a4->sin_family = AF_INET;
            a4->sin_addr = dst4;
            a4->sin_port = htons(port);
            current->addrlen = sizeof(struct sockaddr_in);
        }
        sest->current = current;
    }

    return true;
}

static struct SenderList *parse_senders(const char *ifname, char *senders)
{
    struct SenderState sest = {NULL, NULL};
    if (!foreach_stages(senders, process_sender, &sest)) {
        log_error("udp-in %s senders invalid", ifname);
        delete_senderlist(sest.senders);
        return NULL;
    }

    if (sest.current) {
        delete_senderlist(sest.senders);
        delete_senderlist(sest.current);
        log_error("udp-in %s senders: the last address didn't have an interface name", ifname);
        return NULL;
    }

    return sest.senders;
}

static bool udpin_recv(struct Interface *iface)
{
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;
    (void)uid;

    struct Packet *p = iface_common_recv(iface, NULL, NULL);
    if (p == NULL) return false;
    packet_logcat(p, "%s %u ", iface->name, p->len);
    return iface_common_process(iface, p);
}

static bool udpin_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    log_warning("udp-in interface %s refusing to send packet", iface->name);
    return false;
}

static bool udpin_open(struct Interface *iface)
{
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open udp-in interface %s: already opened", iface->name);
        return false;
    }

    int sock = socket(uid->family, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_perror("udp-in socket");
        return false;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        log_perror("udp-in setsockopt SO_REUSEADDR");
        close(sock);
        return false;
    }

    struct ifreq if_mtu, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, iface->ifname, IFNAMSIZ-1);
    strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
    if (ioctl(sock, SIOCGIFMTU, &if_mtu) < 0) {
        log_perror("udp-in get MTU for %s", iface->ifname);
        close(sock);
        return false;
    }
    if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
        log_perror("udp-in get ifindex for %s", iface->ifname);
        close(sock);
        return false;
    }
    uid->mtu = if_mtu.ifr_mtu;
    uid->ifindex = if_idx.ifr_ifindex;

    if (!set_srcip(iface->ifname, uid)) {
        close(sock);
        return false;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
        log_perror("udp-in setsockopt SO_BINDTODEVICE");
        close(sock);
        return false;
    }

    if (uid->family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(uid->port);
        if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
            log_perror("udp-in bind sock udp6");
            return false;
        }
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(uid->port);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            log_perror("udp-in bind sock udp4");
            return false;
        }
    }

    enable_rx_tstamp(sock, "udp-in", iface->ifname/*, HWTSTAMP_FILTER_ALL*/);
    char if_ip[INET6_ADDRSTRLEN];
    if (HAVE_IP) {
        inet_ntop(uid->family, &uid->srcip, if_ip, sizeof(if_ip));
    }
    else {
        strcpy(if_ip, "<none>");
    }
    log_info("Udp-in interface %s on device %s ip %s port %u", iface->name, iface->ifname, if_ip, uid->port);

    uid->ifmon = thread_launch(iface_address_monitoring, iface, "ifmon %s", iface->name);
    notification_register_source(iface->name, iface_notification_pull_fn, iface, 2000);

    iface->recvfd = sock;
    iface->state = IFS_OPEN;
    return true;
}

static bool udpin_close(struct Interface *iface)
{
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;
    notification_register_source(iface->name, NULL, NULL, 2000);
    close(iface->recvfd);
    thread_stop(uid->ifmon);
    delete_senderlist(uid->senders);
    free(uid);
    log_info("Udp-in interface %s closed", iface->name);
    return true;
}

static void udpin_port_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;
    struct Value val = {&uid->port, 0, 2*8};
    consumer(consumer_state, &val, p);
}

static void udpin_srcip_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct Interface *iface = (struct Interface *)state;
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;
    struct Value val = {&uid->srcip, 0, uid->family == AF_INET6 ? 128u : 32u};
    consumer(consumer_state, &val, p);
}

static value_producer *udpin_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target)
{
    struct UdpInIfData *uid = (struct UdpInIfData *)iface->iface_private;

    if (strcmp(property, "port") == 0) {
        if (target_type != FT_NUMBER) {
            log_error("udpin_get_property_reader 'port' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != 2*8)) {
            log_error("udpin_get_property_reader 'port' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpin_port_producer;
    } else if (strcmp(property, "srcip") == 0) {
        enum ProtocolFieldType ftype = uid->family == AF_INET6 ? FT_IPV6ADDRESS : FT_IPV4ADDRESS;
        unsigned bitcount = uid->family == AF_INET6 ? 128 : 32;
        if (target_type != ftype) {
            log_error("udpin_get_property_reader 'srcip' target type %d invalid", target_type);
            return NULL;
        }
        if ((target->bitoffset % 8) || (target->bitcount != bitcount)) {
            log_error("udpin_get_property_reader 'srcip' target position %u %u invalid",
                    target->bitoffset, target->bitcount);
            return NULL;
        }
        return udpin_srcip_producer;
    }

    log_error("udpin_get_property_reader unknown property '%s'", property);
    return NULL;
}

struct Interface *new_udp_in_interface(const char *name, const char *ifname,
        unsigned port, unsigned ipversion, const char *senders)
{
    _NEW_IFACE(IF_UDP_IN);
    iface->ifname = strdup(ifname);
    iface->recv = udpin_recv;
    iface->send = udpin_send;
    iface->open = udpin_open;
    iface->close_ = udpin_close;
    iface->get_property_reader = udpin_get_property_reader;

    struct SenderList *sender_list = NULL;
    if (senders) {
        char *sender_str = strdup(senders);
        sender_list = parse_senders(ifname, sender_str);
        free(sender_str);
        if (sender_list == NULL) {
            free(iface->name);
            free(iface->ifname);
            free(iface);
            return NULL;
        }

        log_debug("udp-in %s senders:", ifname);
        for (struct SenderList *s=sender_list; s; s=s->next) {
            log_debug("  %s %s port %u %s", (s->family==AF_INET6?"IPv6":"IPv4"), s->ip_str, s->port, s->ifname);
        }
    }

    struct UdpInIfData *uid = calloc_struct(UdpInIfData);
    iface->iface_private = uid;
    uid->port = port;
    uid->family = ipversion == 6 ? AF_INET6 : AF_INET;
    uid->senders = sender_list;

    return iface;
}
