// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_UTILS_H
#define R2_IF_UTILS_H

#include <stdbool.h>

struct Interface;
struct Packet;

struct msghdr;

void enable_rx_tstamp(int sock, const char *sockname,
        const char *ifname/*, enum hwtstamp_rx_filters filter*/);

bool enable_so_txtime(int sock, const char *sockname, const char *ifname, bool deadline);

typedef void msghdr_process_cb(struct msghdr *msg, struct Packet *p, void *userdata);

// the msghdr is passed to the @msg_cb callback if it's not NULL
struct Packet *iface_common_recv(struct Interface *iface, msghdr_process_cb *msg_cb, void *userdata);

bool iface_common_send(struct Interface *iface, struct Packet *p, int socket, void *dst, unsigned dstlen);

bool iface_common_process(struct Interface *iface, struct Packet *p);

// @returns a handle to the monitoring object or NULL on error
void *monitor_error_queue(int socket, int family, const char *name);

// @monitor is the object returned by @monitor_error_queue
void stop_monitoring_error_queue(void *monitor);

struct ifaddrs;
void print_ifaddrs(struct ifaddrs *ifa);

// helper for the type-specific constructors
// TODO reference_count = 1
#define _NEW_IFACE(type_)                               \
    struct Interface *iface = calloc_struct(Interface); \
    iface->name = strdup(name);                         \
    iface->reference_count = 0;                         \
    iface->type = type_;                                \
    iface->state = IFS_INIT

#endif // R2_IF_UTILS_H
