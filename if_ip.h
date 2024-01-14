// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_IP_H
#define R2_IF_IP_H

struct Interface;

struct Interface *new_ip_interface(const char *name, const char *ifname);

#endif // R2_IF_IP_H
