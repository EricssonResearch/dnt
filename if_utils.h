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

typedef void msghdr_process_cb(struct msghdr *msg, struct Packet *p, void *userdata);

// the msghdr is passed to the @msg_cb callback if it's not NULL
struct Packet *iface_common_recv(struct Interface *iface, msghdr_process_cb *msg_cb, void *userdata);

bool iface_common_send(struct Interface *iface, struct Packet *p, int socket, void *dst, unsigned dstlen);

struct ifaddrs;
void print_ifaddrs(struct ifaddrs *ifa);

#endif // R2_IF_UTILS_H
