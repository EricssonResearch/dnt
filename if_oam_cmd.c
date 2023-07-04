// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "oam.h"
#include "if_oam_cmd.h"
#include "if_utils.h"
#include "interface.h"
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
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define BACKLOG 10   // how many pending connections queue will hold

const char help_str[]="Available commands:\nhelp - get help\nexit - exit OAM\nping <stream:mep-start> <mep-stop/mip/any> <level>\ntrace <stream:mep-start> <mep-stop/mip> <level>\ndiscovery <stream:mep-start> <mep-stop/mip> <level>\n";

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

    char oam_command[255];
    char stream[32],mep_start[32], mep_stop[32];
    char resp[255];
    int level;
    int n;

    if (send(oid->oam_cmd_fd, "OAM ready.\n", 12, 0) == -1)
        perror("send");

    while(true){
      n=read(oid->oam_cmd_fd, oam_command, sizeof(oam_command));
      if(n>0){
        if(strncmp(oam_command, "exit",4) == 0){
          if (send(oid->oam_cmd_fd, "Exiting.\n", 9, 0) == -1)
              perror("send");
          break;
        }
        if(strncmp(oam_command, "help",4) == 0){
          if (send(oid->oam_cmd_fd, help_str, sizeof(help_str), 0) == -1)
              perror("send");
        }
        if(strncmp(oam_command, "ping",4) == 0){
          sscanf(oam_command, "ping %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
          cmd_id++;
          sprintf(resp, "OK %d, ping %s : %s -> %s, level %d\n", cmd_id, stream, mep_start, mep_stop, level);
          if (send(oid->oam_cmd_fd, resp, sizeof(resp), 0) == -1)
              perror("send");
          // call the OAM ping function
          oam_ping(cmd_id, stream, mep_start, mep_stop, level);

        }
        if(strncmp(oam_command, "trace",5) == 0){
          sscanf(oam_command, "trace %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
          cmd_id++;
          sprintf(resp, "OK %d, trace %s : %s -> %s, level %d\n", cmd_id, stream, mep_start, mep_stop, level);
          if (send(oid->oam_cmd_fd, resp, sizeof(resp), 0) == -1)
              perror("send");
          // call the OAM trace function
          oam_trace(cmd_id, stream, mep_start, mep_stop, level);
        }
        if(strncmp(oam_command, "discovery",9) == 0){
          sscanf(oam_command, "discovery %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
          cmd_id++;
          sprintf(resp, "OK %d, discovery %s : %s -> %s, level %d\n", cmd_id, stream, mep_start, mep_stop, level);
          if (send(oid->oam_cmd_fd, resp, sizeof(resp), 0) == -1)
              perror("send");
          // call the OAM discovery function
          oam_discovery(cmd_id, stream, mep_start, mep_stop, level);
        }

      }
      else break;
    }

    close(oid->oam_cmd_fd);
    oid->oam_cmd_fd = -1;

    return NULL;
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

    inet_ntop(their_addr.ss_family,
        get_in_addr((struct sockaddr *)&their_addr),
        s, sizeof s);
    printf("OAM server: got connection from %s\n", s);

    if(oid->oam_cmd_fd != -1){  // can not accept multiple OAM connections
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
        fprintf(stderr, "could not create new oam thread\n");
        return false;
    }


    return NULL;
}

static bool oam_cmd_send(struct Interface *iface, struct Packet *p)
{
    (void)p;
    fprintf(stderr, "oam cmd interface %s should not send packet\n", iface->name);
    return false;
}

static bool oam_cmd_open(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    if (iface->state != IFS_INIT) {
        fprintf(stderr, "open OAM cmd interface %s: already opened\n", iface->name);
        return false;
    }
//    if (iface->parse_interfacestree == NULL) {
//        fprintf(stderr, "oam interface %s: no parsetree, expect trouble\n", iface->name);
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
            oid->srcip.v6 = *a6;
            srcip_set = true;
            break;
        } else if (family == AF_INET) {
            oid->srcip.v4 = ((struct sockaddr_in*)(ifa->ifa_addr))->sin_addr;
            srcip_set = true;
            break;
        }
    }
    freeifaddrs(ifaddr);
    if (!srcip_set) {
        fprintf(stderr, "open oam cmd interface %s: no address on interface %s\n", iface->name, iface->ifname);
        close(sock);
        return false;
    }

    if (oid->family == AF_INET6) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(oid->port);
        if (bind(sock, (struct sockaddr*)&addr6, sizeof(addr6)) < 0) {
            perror("oam bind sock6");
            return false;
        }
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(oid->port);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("oam bind sock");
            return false;
        }
    }

    if (listen(sock, BACKLOG) == -1) {
        perror("oam cmd listen");
        exit(1);
    }

    iface->recvfd = sock;
    iface->state = IFS_OPEN;
    return true;
}

static bool oam_cmd_close(struct Interface *iface)
{
    struct OamCmdIfData *oid = iface->iface_private;
    close(iface->recvfd);
    free(oid);
    return true;
}


bool init_oam_cmd_interface(struct Interface *iface, const char *name, const char *ifname,
        unsigned port, unsigned ipversion)
{
    bzero(iface, sizeof(*iface));
    iface->name = strdup(name);
    if(ifname != NULL)
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
    oid->oam_cmd_fd = -1;

    return true;
}
