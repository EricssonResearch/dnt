
#include "delay.h"
#include "interface.h"
#include "if_eth.h"
#include "packet.h"
#include "parsetree.h"
#include "pipeline.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10

static void recv_loop(struct Interface *ifaces, unsigned iface_count)
{
    int epollfd = epoll_create1(0);
    if (epollfd < 0) {
        perror("epoll_create1");
        return;
    }

    for (unsigned i=0; i<iface_count; i++) {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = &ifaces[i];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ifaces[i].recvfd, &ev) < 0) {
            perror("epoll_ctl"); //TODO better error message
            //TODO return?
        }
    }

    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            return;
        }

        for (int n=0; n<nfds; n++) {
            struct Interface *recvif = events[n].data.ptr;
            struct Packet *p = new_packet(recvif);
            recvif->recv(recvif, p);
            if (packet_dummy(p)) {
                fprintf(stderr, "packet overflow, received on interface %s\n", recvif->name);
                delete_packet(p);
            } else {
                struct Pipeline *pipe = parsetree_process(recvif->parsetree, p);
                if (pipe == NULL) {
                    fprintf(stderr, "no pipeline found for packet on %s, unknown stream\n", recvif->name);
                    delete_packet(p);
                } else {
                    // the iterator owns the packet
                    struct PipelineIterator *pi = new_pipe_iterator(pipe, p);
                    // the iterator deletes itself when it's done
                    pipe_iterator_run(pi);
                }
            }
        }
    }
}

int main(void)
{
    printf("R2DTWO - Reliable & Robust Deterministic Tool for netWOrking implementation version %d.%d\n",
            VERSION_MAJOR, VERSION_MINOR);

    //TODO argument: config file name

    //TODO read config
    //TODO init interfaces based on config

    //TODO replace these with the proper ones from config
    struct Interface tmp_ifaces[3];
    init_eth_interface(tmp_ifaces+0, "tmp0", "eth0");
    init_eth_interface(tmp_ifaces+1, "tmp1", "eth1");
    init_eth_interface(tmp_ifaces+2, "tmp2", "eth2");
    //TODO add dummy parsetrees so we can test the pipelines

    if (!init_delay()) return -1;

    recv_loop(tmp_ifaces, 3);

    fini_delay();

    return 0;
}
