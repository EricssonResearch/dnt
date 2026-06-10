
// minimal model of the packet forwarding of DNT with eth interfaces

// cc -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wvla -Wc++-compat -Werror perf_tester.c -o perf_tester

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>

#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h> /* struct ifreq */
#include <net/ethernet.h> /* struct ether_addr */

#include <linux/if_ether.h> /* ETH_P_ALL */
#include <linux/if_packet.h> /* struct sockaddr_ll, PACKET_AUXDATA TODO netpacket/packet.h? */

enum Mode { MODE_POLL, MODE_THREAD };

int sigint_count = 0;

#define MAX_EVENTS 10

struct Iface {
    int recv;
    int send;
    int ifindex;
};

static void sigint_handler(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    (void)uc;

    printf("%s signal caught\n", strsignal(sig));
    sigint_count++;
}

static int create_send_socket(const char *ifname, struct Iface *iface)
{
    printf("creating socket for '%s'\n", ifname);

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("if_eth socket");
        return -1;
    }
    struct ifreq if_mtu, if_mac, if_idx;
    memset(&if_mtu, 0, sizeof(struct ifreq));
    memset(&if_mac, 0, sizeof(struct ifreq));
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_mtu.ifr_name, ifname, IFNAMSIZ-1);
    strncpy(if_mac.ifr_name, ifname, IFNAMSIZ-1);
    strncpy(if_idx.ifr_name, ifname, IFNAMSIZ-1);
    if (ioctl(sock, SIOCGIFMTU, &if_mtu) < 0) {
        perror("SIOCGIFMTU");
        close(sock);
        return -1;
    }
    if (ioctl(sock, SIOCGIFHWADDR, &if_mac) < 0) {
        perror("SIOCGIFHWADDR");
        close(sock);
        return -1;
    }
    if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
        perror("SIOCGIFINDEX");
        close(sock);
        return -1;
    }
    int enable = 1;
    if (setsockopt(sock, SOL_PACKET, PACKET_AUXDATA, &enable, sizeof(enable)) < 0) {
        perror("setsockopt PACKET_AUXDATA");
        close(sock);
        return -1;
    }

    //TODO enable_rx_tstamp(sock, "eth", iface->ifname/*, HWTSTAMP_FILTER_ALL*/);

    // bind to device
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_protocol = htons(ETH_P_ALL);
    socket_address.sll_ifindex = if_idx.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0) {
        perror("bind sock to iface");
        close(sock);
        return -1;
    }

    // set iface to promiscuous mode
    struct packet_mreq mreq;
    mreq.mr_type = PACKET_MR_PROMISC;
    mreq.mr_ifindex = if_idx.ifr_ifindex;
    mreq.mr_alen = 0;
    memset(&mreq.mr_address, 0, sizeof(mreq.mr_address));
    if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt mreq promisc");
        close(sock);
        return -1;
    }

    // Ignore outgoing packets sent on other priority sockets (since Linux 4.20)
    int true_flag = 1;
    if (setsockopt(sock, SOL_PACKET, PACKET_IGNORE_OUTGOING, &true_flag, sizeof(true_flag)) < 0) {
        perror("setsockopt PACKET_IGNORE_OUTGOING");
        close(sock);
        return -1;
    }

    iface->send = sock;
    iface->ifindex = if_idx.ifr_ifindex;
    return 1;
}

static void if_send(struct Iface *out, char *buf, unsigned buflen)
{
    //printf("send %u bytes on %d\n", buflen, out->send);

    //TODO delete vlan

    char *dst_mac = buf;
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_ifindex = out->ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, dst_mac, ETH_ALEN);

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &socket_address;
    msg.msg_namelen = sizeof(socket_address);

    struct iovec iov[1];
    iov[0].iov_base = buf;
    iov[0].iov_len = buflen;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    if (sendmsg(out->send, &msg, 0) < 0) {
        perror("sendmsg");
    }
    free(buf);
}

static void if_recv(struct Iface *in)
{
    //char recvbuf[9000];
    char *recvbuf = (char*)malloc(9000*sizeof(char));
    char control[1000]
        __attribute__ ((aligned(__alignof__(struct cmsghdr))));
    struct msghdr msg;
    struct iovec iov;
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = recvbuf;
    iov.iov_len = 9000;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    int res = recvmsg(in->recv, &msg, 0);

    if (res < 0) {
        perror("recvmsg");
        return;
    }
    if (res == 0) {
        return;
    }

    //TODO add vlan

    //printf("recv %u bytes on %d\n", res, in->recv);

    return if_send(in, recvbuf, res);
}

static void *recv_thread(void *arg)
{
    struct Iface *iface = (struct Iface *)arg;

    while (1) {
        if_recv(iface);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    enum Mode mode = MODE_POLL;
    struct Iface ifaces[2];

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s [mode] iface1 iface2\n"
                "   where mode can be -poll or -thread\n", argv[0]);
        return EXIT_FAILURE;
    } else if (argc == 3) {
        if (create_send_socket(argv[1], &ifaces[0]) < 0)
            return EXIT_FAILURE;

        if (create_send_socket(argv[2], &ifaces[1]) < 0)
            return EXIT_FAILURE;
    } else if (argc == 4) {
        if (strcmp(argv[1], "-poll") == 0) {
            mode = MODE_POLL;
        } else if (strcmp(argv[1], "-thread") == 0) {
            mode = MODE_THREAD;
        } else {
            fprintf(stderr, "invalid mode '%s'\n", argv[1]);
            return EXIT_FAILURE;
        }
        if (create_send_socket(argv[2], &ifaces[0]) < 0)
            return EXIT_FAILURE;

        if (create_send_socket(argv[3], &ifaces[1]) < 0)
            return EXIT_FAILURE;
    }
    ifaces[0].recv = ifaces[1].send;
    ifaces[1].recv = ifaces[0].send;

    struct sigaction sa;
    //struct sigevent sev;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigint_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction for SIGINT");
        return EXIT_FAILURE;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {    // SIGTERM is received when terminated via kill
        perror("sigaction for SIGTERM");
        return EXIT_FAILURE;
    }

    //TODO start a background thread that does lots of malloc/free

    if (mode == MODE_POLL) {
        printf("entering poll mode\n");

        // epoll like r2d2
        int epollfd = epoll_create1(0);
        if (epollfd < 0) {
            perror("epoll_create1 failed");
            return EXIT_FAILURE;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = &ifaces[0];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ifaces[0].recv, &ev) < 0) {
            perror("add socket0 to epoll");
            return EXIT_FAILURE;
        }
        ev.data.ptr = &ifaces[1];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ifaces[1].recv, &ev) < 0) {
            perror("add socket1 to epoll");
            return EXIT_FAILURE;
        }

        struct epoll_event events[MAX_EVENTS];
        while (sigint_count == 0) {
            int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
            if (nfds == -1) {
                // a signal, keep receiving
                if (errno == EINTR) {
                    continue;
                }
                perror("epoll_wait");
                break;
            }

            for (int n=0; n<nfds; n++) {
                struct Iface *pi = (struct Iface*) events[n].data.ptr;
                if_recv(pi);
            }
        }
    } else {
        pthread_t th0;
        pthread_t th1;

        printf("entering thread mode\n");

        errno = pthread_create(&th0, NULL, recv_thread, &ifaces[0]);
        if (errno != 0) {
            perror("failed to start thread0");
            return EXIT_FAILURE;
        }
        errno = pthread_create(&th1, NULL, recv_thread, &ifaces[1]);
        if (errno != 0) {
            perror("failed to start thread1");
            return EXIT_FAILURE;
        }

        while (sigint_count == 0) {
            sleep(1);
        }

        pthread_cancel(th0);
        pthread_cancel(th1);
        pthread_join(th0, NULL);
        pthread_join(th1, NULL);
    }

    printf("receive loop ended\n");

    close(ifaces[0].recv);
    close(ifaces[0].send);

    return EXIT_SUCCESS;
}
