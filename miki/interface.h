
#ifndef R2_INTERFACE_H
#define R2_INTERFACE_H

#include <stdbool.h>

struct Interface;
struct Packet;
struct ParseTree;

enum IfaceType {
    IF_ETH,
    IF_IP4,
    IP_IP6,
    IF_UDP_IN,
    IF_UDP_OUT,
};

// receive a packet on @fd
// puts the received data into the @packet
// @returns false if the packet reception failed
typedef bool iface_recv(struct Interface *iface, struct Packet *p);

// sends the packet on the interface
typedef void iface_send(struct Interface *iface, struct Packet *p);

// finishes the interface
// @returns false on error
typedef bool iface_del(void *iface_private);

struct Interface {
    enum IfaceType type;
    char *name;
    char *ifname;
    int recvfd;
    iface_recv *recv;
    iface_send *send;
    iface_del *del;
    void *iface_private;

    struct ParseTree *parsetree;

};

// finishes the interface but doesn't free the given pointer
void fini_interface(struct Interface *iface);

#endif // R2_INTERFACE_H
