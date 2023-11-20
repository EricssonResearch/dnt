// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_oam_cmd.h"
#include "interface.h"
#include "log.h"
#include "oam.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>

#include <errno.h>
#include <netinet/in.h>

DEFAULT_LOGGING_MODULE(OAM, LOG_WARNING)
#define BACKLOG 2   // how many pending connections queue will hold

struct OamCmdIfData {
    int oam_cmd_fd;
    FILE *oam_cmd_w;
    enum TerminalFormat mode;
    pthread_t oam_tid;
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

static void *oam_cmd_thread(void *arg)
{
    struct Interface *iface = (struct Interface *)arg;
    struct OamCmdIfData *oid = iface->iface_private;

    // note: inverse operation is fd=fileno(file)
    FILE *cmd_w = fdopen(oid->oam_cmd_fd, "w");
    //setvbuf(cmd_w, NULL, _IOLBF, 0);
    //TODO if we want to fread() we need to duplicate the handle
    //int cmd_fd_dup = dup(cmd_fd);
    //FILE *cmd_r = fdopen(cmd_fd_dup, "r");

    setvbuf(cmd_w, NULL, _IOLBF, 0);
    oid->oam_cmd_w = cmd_w;

    oam_command_loop(iface);

    fclose(cmd_w);
    //close(oid->oam_cmd_fd);
    oid->oam_cmd_fd = -1;

    return NULL;
}

int oam_get_cmd_fd(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    return oid->oam_cmd_fd;
}

FILE *oam_get_cmd_w(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    return oid->oam_cmd_w;
}

int oam_cmd_recv_reply(struct Interface *iface, const char *msg){
    struct OamCmdIfData *oid = iface->iface_private;
    int oam_cmd_fd = oid->oam_cmd_fd;
    if(oam_cmd_fd != -1){
        if (send(oam_cmd_fd, msg, strlen(msg), 0) == -1)
            perror("send");
    }
    else
        printf("OAM message, no command channel open: %s\n", msg);

    return 0;
}

enum TerminalFormat oam_cmd_get_mode(struct Interface *iface)
{
    return ((struct OamCmdIfData *)(iface->iface_private))->mode;
}

void oam_cmd_set_mode(struct Interface *iface, enum TerminalFormat mode)
{
    ((struct OamCmdIfData *)(iface->iface_private))->mode = mode;
    return;
}


static struct Packet *oam_cmd_recv(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;

    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    struct sockaddr_storage their_addr; // connector's address information
    sin_size = sizeof their_addr;
    int new_fd = accept(iface->recvfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
        perror("accept");
    }

    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    printf("OAM server: got connection from %s\n", s);

    if (oid->oam_cmd_fd != -1) {  // can not accept multiple OAM connections
        if (send(new_fd, "OAM busy.\n", 10, 0) == -1)
            perror("send");
        close(new_fd);
        return NULL;
    }

    oid->oam_cmd_fd = new_fd;

    pthread_attr_t attr;
    if ((errno = pthread_attr_init(&attr)) != 0) {
        perror("oam thread pthread_attr_init");
        return false;
    }

    if (pthread_create(&oid->oam_tid, &attr, &oam_cmd_thread, iface) != 0) {
        perror("could not create new oam thread");
        return false;
    }

    return NULL;
}

static bool oam_cmd_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    log_error("oam cmd interface %s should not send packet\n", iface->name);
    return false;
}

static bool oam_cmd_open(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        log_error("open OAM cmd interface %s: already opened\n", iface->name);
        return false;
    }
    //    if (iface->parse_interfacestree == NULL) {
    //        log_error("oam interface %s: no parsetree, expect trouble\n", iface->name);
    //TODO fatal?
    //    }
    int sock = socket(oid->family, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("oam cmd socket");
        return false;
    }

    int enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("oam cmd setsockopt SO_REUSEADDR");
        close(sock);
        return false;
    }

    struct ifaddrs *ifaddr;
    if(iface->ifname != 0) {
        struct ifreq  if_idx;
        memset(&if_idx, 0, sizeof(struct ifreq));
        strncpy(if_idx.ifr_name, iface->ifname, IFNAMSIZ-1);
        if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
            perror("oam cmd SIOCGIFINDEX");
            close(sock);
            return false;
        }
        //      oid->ifindex = if_idx.ifr_ifindex;
    }

    if (getifaddrs(&ifaddr) < 0) {
        perror("oam cmd getifaddrs");
        close(sock);
        return false;
    }

    bool srcip_set = false;
    for (struct ifaddrs *ifa=ifaddr; ifa!=NULL; ifa=ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != oid->family) continue;
        if ( (iface->ifname != NULL) && strcmp(ifa->ifa_name, iface->ifname) != 0) continue;

        //print_ifaddrs(ifa);

        if (family == AF_INET6) {
            struct in6_addr *a6 = &((struct sockaddr_in6*)(ifa->ifa_addr))->sin6_addr;
            if (IN6_IS_ADDR_LINKLOCAL(a6)) continue;
            // If not "INADDR6_ANY" given, check to match IPv6 address
            if ((!IN6_IS_ADDR_UNSPECIFIED(&oid->srcip.v6)) && (memcmp(&oid->srcip.v6, a6, sizeof(struct in6_addr)) != 0))
                continue;
            srcip_set = true;
            break;
        } else if (family == AF_INET) {
            // If not "INADDR_ANY" given, check to match IPv4 address
            if( (oid->srcip.v4.s_addr != INADDR_ANY) && (oid->srcip.v4.s_addr != ((struct sockaddr_in*)(ifa->ifa_addr))->sin_addr.s_addr) )
                continue;
            srcip_set = true;
            break;
        }
    }
    freeifaddrs(ifaddr);
    if (!srcip_set) {
        log_error("open oam cmd interface %s: no address or address mismatch on interface\n", iface->name);
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
            perror("oam bind sock6");
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
            perror("oam bind sock");
            close(sock);
            return false;
        }
    }

    if (listen(sock, BACKLOG) == -1) {
        perror("oam cmd listen");
        close(sock);
        return false;
    }

    iface->recvfd = sock;
    if (set_oam_cmd_if(iface)) {
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
    return true;
}


bool init_oam_cmd_interface(struct Interface *iface, const char *name, const char *ifname,
                            const char *oam_cmd_ip, unsigned port, unsigned ipversion)
{
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    if (ifname != NULL)
        if(strcmp(ifname,"any")==0)
          iface->ifname = NULL;
        else
          iface->ifname = strdup(ifname);
    else
      iface->ifname = NULL;
    iface->type = IF_OAM_CMD;
    iface->state = IFS_INIT;
    iface->recv = oam_cmd_recv;
    iface->send = oam_cmd_send;
    iface->open = oam_cmd_open;
    iface->close_ = oam_cmd_close;

    struct OamCmdIfData *oid = calloc_struct(OamCmdIfData);
    iface->iface_private = oid;
    oid->port = port;
    oid->family = ipversion == 6 ? AF_INET6 : AF_INET;
    log_info("Family: %d\n", oid->family );
    if(oid->family == AF_INET6){
      inet_pton(AF_INET6, oam_cmd_ip, &(oid->srcip.v6));
    } else {
      inet_pton(AF_INET, oam_cmd_ip, &(oid->srcip.v4));
    }
    oid->mode = DUMP;
    oid->oam_cmd_fd = -1;

    return true;
}
