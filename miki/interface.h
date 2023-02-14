
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
// @returns false if the packet sending failed
typedef bool iface_send(struct Interface *iface, struct Packet *p);

// finishes the interface
// @fini_interface frees @name and @ifname so this callback doesn't have to
// @returns false on error
typedef bool iface_del(struct Interface *iface);

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

    //TODO properties that actions can query: MAC address, IP address, port etc.
    //  TODO we need a per-type list of the names for the config validator
    //  TODO we need a per-instance hash to query the values

    //TODO we should do a refcounting of the interfaces
    //      pipelines hold a ref to the interfaces they send on
    //      iface is not deleted while we have a packet in the system that will be sent out on that iface
    //      if iface is marked for deletion (but still having ref) stops receiving packets
};

// finishes the interface but doesn't free the given pointer (iface is in an array!)
void fini_interface(struct Interface *iface);

#endif // R2_INTERFACE_H
