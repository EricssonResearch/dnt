// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "configfile.h"
#include "delay.h"
#include "hashmap.h"
#include "interface.h"
#include "if_eth.h"
#include "packet.h"
#include "parsetree.h"
#include "pipeline.h"
#include "protocol.h"
#include "seq_gen.h"
#include "time_utils.h"
#include "oam.h"
#include "log.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

#include <signal.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10

DEFAULT_LOGGING_MODULE(MAIN, LOG_INFO);
LOGGING_MODULE(CONFIG, LOG_INFO);
LOGGING_MODULE(PACKET, LOG_INFO);

int sigint_count = 0;
struct R2d2Config *config;

static void sigint_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig;
    (void)si;
    (void)uc;


    printf("SIGINT or SIGTERM caught\n");
    sigint_count++;
}

static void sigusr1_handler(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    (void)uc;

    if (sig != SIGUSR1)
        return;
    printf("SIGUSR1 caught\n");

    hashmap_foreach(config->objects, reset_all_seq_generators, NULL);
}

static void recv_loop(struct Interface *ifaces, unsigned iface_count)
{
    struct sigaction sa;
    //struct sigevent sev;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigint_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {    // SIGTERM is received when terminated via kill
        perror("sigaction");
        return;
    }

    sa.sa_sigaction = sigusr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction");
        return;
    }

    int epollfd = epoll_create1(0);
    if (epollfd < 0) {
        perror("epoll_create1");
        return;
    }

    for (unsigned i=0; i<iface_count; i++) {
        if (ifaces[i].recvfd == 0) continue;

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = &ifaces[i];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ifaces[i].recvfd, &ev) < 0) {
            perror("epoll_ctl"); //TODO better error message
            //TODO return?
        }
    }

    struct timespec last_perfcheck_time;
    clock_gettime(CLOCK_REALTIME, &last_perfcheck_time);
    struct epoll_event events[MAX_EVENTS];
    while (sigint_count == 0) {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            // a signal, keep receiving
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            return;
        }

        for (int n=0; n<nfds; n++) {
            struct Interface *recvif = events[n].data.ptr;
            struct Packet *p = recvif->recv(recvif);
            if (p == NULL)
                continue;
            //printf("received packet length %u on %s\n", p->len, recvif->name);
            struct Pipeline *pipe = parsetree_process(recvif->parsetree, p);
            if (pipe == NULL) {
#ifdef VERBOSE_RECV
                printf("no pipeline found, unknown stream\n");
#endif
                delete_packet(p);
            } else {
#ifdef VERBOSE_RECV
                printf("parsetree identified %u headers, pipe %s\n", p->header_count, pipe->name);
                for (unsigned i=0; i<p->header_count; i++) {
                    printf("  header %u is %s at %u len %u\n", i,
                            protocol_list[p->headers[i].type].name, p->headers[i].start, p->headers[i].len);
                }
#endif
                // the iterator owns the packet
                struct PipelineIterator *pi = new_pipe_iterator(pipe, p);
                // the iterator deletes itself when it's done
                pipe_iterator_run(pi);
            }
        }

        struct timespec now, diff;
        clock_gettime(CLOCK_REALTIME, &now);
        timespecsub(&now, &last_perfcheck_time, &diff);
        unsigned diff_msec;
        timespec_to_msec(diff_msec, &diff);
        if (diff_msec > 2000) {
            packets_check_performance();
            last_perfcheck_time = now;
        }
    }
}

int main(int argc, char **argv)
{
    printf("R2DTWO - Reliable & Robust Deterministic Tool for netWOrking implementation\n"
            "Version %d.%d\n", VERSION_MAJOR, VERSION_MINOR);

    if (argc < 2) {
        fprintf(stderr, "usage: %s configfile\n", argv[0]);
        return -1;
    }

    char log_filename[PATH_MAX];
    pid_t pid = getpid();
    char *start = argv[1];
    for (char *c=argv[1]; *c; c++) {
      if (*c == '/') start = ++c;
    }
    char *end = NULL;
    for (char *c=start; *c; c++) {
      if (*c == '.') end = c;
    }
    char *confname = end ? strndup(start, end-start) : strdup(start);
    sprintf(log_filename, "r2dtwo-%s-%u.log", confname, pid);
    if (!open_log(log_filename)) {
        return -1;
    }
    free(confname);
    log_info("Logfile opened.");

    log_info_m(CONFIG, "Reading config '%s'", argv[1]);
    config = read_config(argv[1]);
    if (config == NULL) {
        fprintf(stderr, "the config is invalid\n");
        return -1;
    }

    //TODO do this in read_config()?
    for (unsigned i=0; i<config->ifcount; i++) {
        iface_set_parsetree(&config->ifaces[i], new_parsetree(&config->ifaces[i]));
    }

    if (!config_add_streams_to_interfaces(config)) {
        delete_config(config);
        return -1;
    }

    for (unsigned i=0; i<config->ifcount; i++) {
        if (!config->ifaces[i].open(&config->ifaces[i])) {
            fprintf(stderr, "could not open interface %s\n", config->ifaces[i].name);
            delete_config(config);
            return -1;
        }
    }

    if (!init_delay()) {
        delete_config(config);
        return -1;
    }

    if(!init_oam(config->oam)) {
      printf("OAM init failed\n");
      delete_config(config);
      return -1;
    }

    recv_loop(config->ifaces, config->ifcount);
    printf("receive loop ended\n");

    finish_oam();

    fini_delay();

    delete_config(config);

    close_log();

    return 0;
}
