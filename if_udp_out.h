
#ifndef R2_IF_UDP_OUT_H
#define R2_IF_UDP_OUT_H

#include <stdbool.h>

struct Interface;

bool init_udp_out_interface(struct Interface *iface, const char *name, const char *ifname,
        unsigned src_port, const char *dst_ip, unsigned dst_port, unsigned priority);

#endif // R2_IF_UDP_OUT_H
