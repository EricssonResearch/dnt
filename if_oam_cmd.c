// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam_cmd.h"
#include "interface.h"
#include "log.h"
#include "oam.h"
#include "packet.h"
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

#define BACKLOG 2   // how many pending connections queue will hold

struct OamCmdIfData {
    unsigned port;
    int family;
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } srcip;
};

void *get_in_addr(struct sockaddr *sa);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
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
    struct sockaddr_storage their_addr; // connector's address information
    sin_size = sizeof their_addr;
    int new_fd = accept(iface->recvfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
        log_perror("oam cmd accept");
    }

    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    log_info("got connection from %s", s);

    oam_start_command_connection(new_fd);
    return true;
}

static bool oam_cmd_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    log_error("oam cmd interface %s should not send packet", iface->name);
    return false;
}

static bool oam_cmd_open(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    if (iface->state != IFS_INIT) {
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
        log_info("OAM Command interface on IPv%d", oid->family==AF_INET6?6:4);
        iface->state = IFS_OPEN;
        return true;
    } else {
        close(sock);
        return false;
    }
}

static bool oam_cmd_close(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    close(iface->recvfd);
    free(oid);
    log_info("OAM Command interface closed");
    return true;
}


struct Interface *new_oam_cmd_interface(const char *name, const char *ifname,
                            const char *oam_cmd_ip, unsigned port)
{
    struct Interface *iface = calloc_struct(Interface);
    iface->name = strdup(name);
    if (ifname != NULL) {
        if(strcmp(ifname,"any")==0)
            iface->ifname = NULL;
        else
            iface->ifname = strdup(ifname);
    } else {
        iface->ifname = NULL;
    }
    iface->type = IF_OAM_CMD;
    iface->state = IFS_INIT;
    iface->recv = oam_cmd_recv;
    iface->send = oam_cmd_send;
    iface->open = oam_cmd_open;
    iface->close_ = oam_cmd_close;

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
            return false;
        }
    } else {
        // this will also accept v4 connections
        oid->family = AF_INET6;
    }

    return iface;
}
