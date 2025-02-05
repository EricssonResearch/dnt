// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_UTILS_H
#define R2_IF_UTILS_H

#include "notification.h"

#include <stdbool.h>

struct Interface;
struct Packet;

struct msghdr;

void enable_rx_tstamp(int sock, const char *sockname,
        const char *ifname/*, enum hwtstamp_rx_filters filter*/);

bool enable_so_txtime(int sock, const char *sockname, const char *ifname, bool deadline);

typedef void msghdr_process_cb(struct msghdr *msg, struct Packet *p, void *userdata);

// receives a packet on @iface
// blocks if there is nothing in the receive buffer!
// @returns NULL on error, or when we have too many packets (never returns dummy packet)
// the msghdr is passed to the @msg_cb callback if it's not NULL
struct Packet *iface_common_recv(struct Interface *iface, msghdr_process_cb *msg_cb, void *userdata);

// sends a packet on @socket, which can be different from @iface->recvfd
// @dst and @dstlen should be compatible with the type of @socket
// @returns true on succes
bool iface_common_send(struct Interface *iface, struct Packet *p, int socket, void *dst, unsigned dstlen);

// queries the parsetree of @iface, and runs the returned iterator
// @returns true if a known stream was found for @p
bool iface_common_process(struct Interface *iface, struct Packet *p);

struct MonitorState;

// @returns a handle to the monitoring object or NULL on error
struct MonitorState *monitor_error_queue(int socket, int family, const char *name);

// always @returns NULL
struct MonitorState *stop_monitoring_error_queue(struct MonitorState *st);

// @self must be struct Interface
NotificationLevel iface_notification_pull_fn(void *self, struct JsonValue **msg);

struct ifaddrs;
void print_ifaddrs(struct ifaddrs *ifa);

// helper for the type-specific constructors
#define _NEW_IFACE(type_)                               \
    struct Interface *iface = calloc_struct(Interface); \
    iface->name = strdup(name);                         \
    iface->reference_count = 1;                         \
    iface->type = type_;                                \
    iface->state = IFS_INIT

#endif // R2_IF_UTILS_H
