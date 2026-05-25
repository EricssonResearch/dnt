// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam_cmd.h"
#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "oam.h"
#include "packet.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>

#include <netinet/in.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

#define BACKLOG 2   // how many pending connections the queue will hold

struct OamCmdIfData {
    unsigned port;
    int family;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } srcip;
};

// get sockaddr, IPv4 or IPv6:
static void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static bool oam_cmd_recv(struct Interface *iface)
{
    //struct OamCmdIfData *oid = iface->iface_private;

    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    struct sockaddr_storage their_addr; // remote's address information
    sin_size = sizeof their_addr;
    int new_fd = accept(iface->recvfd, (struct sockaddr *)&their_addr, &sin_size);
    if (iface->state == IFSTATE_SHUTDOWN) {
        if (new_fd > 0)
            close(new_fd);
        return false;
    }
    if (new_fd < 0) {
        log_perror("oam cmd accept");
        return false;
    }

    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    // sockaddr_in6 has the port at the same offset
    struct sockaddr_in *sa4 = (struct sockaddr_in *)&their_addr;
    log_debug("oam_cmd connection from %s port %u", s, ntohs(sa4->sin_port));

    oam_start_command_connection(new_fd, s, ntohs(sa4->sin_port));
    return true;
}

static void *oam_cmd_recv_loop(void *arg)
{
    struct Interface *iface = (struct Interface *)arg;

    while (iface->state != IFSTATE_SHUTDOWN)
        oam_cmd_recv(iface);

    return NULL;
}

static bool oam_cmd_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    log_error("oam cmd interface %s should not send packet", iface->name);
    return false;
}

static bool oam_cmd_open(struct Interface *iface)
{
    struct OamCmdIfData *oid = (struct OamCmdIfData *)iface->iface_private;
    if (iface->state != IFSTATE_INIT) {
        log_error("open OAM cmd interface %s: already opened", iface->name);
        return false;
    }
    int sock = socket(oid->family, SOCK_STREAM, 0);
    if (sock < 0) {
        log_perror("socket");
        return false;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        log_perror("setsockopt SO_REUSEADDR");
        close(sock);
        return false;
    }

    if (oid->family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = oid->srcip.v6;
        addr6.sin6_port = htons(oid->port);
        if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
            log_perror("bind sock6");
            close(sock);
            return false;
        }
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr = oid->srcip.v4;
        addr.sin_port = htons(oid->port);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            log_perror("bind sock4");
            close(sock);
            return false;
        }
    }

    if (iface->ifname) {
        //TODO check that the given interface has the given ip?
        if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, iface->ifname, strlen(iface->ifname)) < 0) {
            log_perror("setsockopt SO_BINDTODEVICE");
            close(sock);
            return false;
        }
    }

    if (listen(sock, BACKLOG) == -1) {
        log_perror("listen");
        close(sock);
        return false;
    }

    iface->recvfd = sock;
    if (set_oam_cmd_if(iface)) {
        log_info("OAM Command interface on IPv%d port %u", oid->family==AF_INET6?6:4, oid->port);
        iface->state = IFSTATE_OPEN;
        iface->recv_th_ = thread_launch(oam_cmd_recv_loop, iface, "cmd %s", iface->name);
        return true;
    } else {
        close(sock);
        return false;
    }
}

static bool oam_cmd_close(struct Interface *iface)
{
    struct OamCmdIfData *oid = (struct OamCmdIfData *)iface->iface_private;
    close(iface->recvfd);
    free(oid);
    log_info("OAM Command interface closed");
    return true;
}

static void oam_cmd_print_private_info(const struct Interface *iface, FILE *cmd_w)
{
    struct OamCmdIfData *oid = (struct OamCmdIfData *)iface->iface_private;

    if (oid->family == AF_INET) {
        char buf4[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &oid->srcip.v4, buf4, sizeof(buf4))) {
            fprintf(cmd_w, "    inet \033[35m%s\033[0m port %u\n", buf4, oid->port);
        } else {
            fprintf(cmd_w, "    inet \033[35m<invalid>\033[0m port %u\n", oid->port);
        }
    } else if (oid->family == AF_INET6) {
        char buf6[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, &oid->srcip.v6, buf6, sizeof(buf6))) {
            fprintf(cmd_w, "    inet6 \033[34m%s\033[m port %u\n", buf6, oid->port);
        } else {
            fprintf(cmd_w, "    inet6 \033[34m<invalid>\033[m port %u\n", oid->port);
        }
    } else {
        fprintf(cmd_w, "    <invalid adress family> port %u\n", oid->port);
    }
}

struct Interface *new_oam_cmd_interface(const char *name, const char *ifname,
                            const char *oam_cmd_ip, unsigned port)
{
    _NEW_IFACE(IF_OAM_CMD);
    if (ifname != NULL) {
        if(strcmp(ifname,"any")==0)
            iface->ifname = NULL;
        else
            iface->ifname = strdup(ifname);
    } else {
        iface->ifname = NULL;
    }
    iface->send = oam_cmd_send;
    iface->open = oam_cmd_open;
    iface->close_ = oam_cmd_close;
    iface->print_private_info = oam_cmd_print_private_info;

    struct OamCmdIfData *oid = calloc_struct(OamCmdIfData);
    iface->iface_private = oid;
    oid->port = port;
    if (oam_cmd_ip) {
        if (inet_pton(AF_INET, oam_cmd_ip, &(oid->srcip.v4)) == 1) {
            oid->family = AF_INET;
        } else if (inet_pton(AF_INET6, oam_cmd_ip, &(oid->srcip.v6)) == 1) {
            oid->family = AF_INET6;
        } else {
            log_error("oam-cmd: invalid ip address '%s'", oam_cmd_ip);
            return NULL;
        }
    } else {
        // this will also accept v4 connections
        oid->family = AF_INET6;
    }

    return iface;
}
