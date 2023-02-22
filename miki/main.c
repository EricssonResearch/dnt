
#include "configfile.h"
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
            struct Packet *p = recvif->recv(recvif);
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

int main(int argc, char **argv)
{
    printf("R2DTWO - Reliable & Robust Deterministic Tool for netWOrking implementation\n"
            " version %d.%d\n", VERSION_MAJOR, VERSION_MINOR);

    if (argc < 2) {
        fprintf(stderr, "usage: %s configfile\n", argv[0]);
        return -1;
    }

    struct R2d2Config *config = read_config(argv[1]);
    if (config == NULL) {
        fprintf(stderr, "the config is invalid\n");
        return -1;
    }

    //TODO build the parse trees and the action pipes here
    //TODO add the parse trees to the interfaces
    //TODO for i (config->ifaces) i->open

    //TODO replace these with the proper ones from config
    /*struct Interface tmp_ifaces[3];
    init_eth_interface(tmp_ifaces+0, "tmp0", "eth0");
    init_eth_interface(tmp_ifaces+1, "tmp1", "eth1");
    init_eth_interface(tmp_ifaces+2, "tmp2", "eth2");*/
    //TODO add dummy parsetrees so we can test the pipelines

    if (!init_delay()) return -1;

    recv_loop(config->ifaces, config->ifcount);

    fini_delay();

    return 0;
}
