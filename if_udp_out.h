// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_UDP_OUT_H
#define R2_IF_UDP_OUT_H

struct Interface;

struct Interface *new_udp_out_interface(const char *name, const char *ifname,
        unsigned src_port, const char *dst_ip, unsigned dst_port, unsigned priority);

#endif // R2_IF_UDP_OUT_H
