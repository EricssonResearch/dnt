
#ifndef R2_INTERFACE_H
#define R2_INTERFACE_H

#include "protocol.h"
#include "transfer.h"

#include <stdbool.h>

struct Interface;
struct Packet;
struct ParseTree;

enum IfaceType {
    IF_ETH = 1,
    IF_IP4,
    IP_IP6,
    IF_UDP_IN,
    IF_UDP_OUT,
};

// receive a packet on @fd
// blocks if no packet is in the rx queue!
// @returns the receivec packet or NULL if reception failed
typedef struct Packet *iface_recv(struct Interface *iface);

// sends the packet on the interface
// @returns false if the packet sending failed
typedef bool iface_send(struct Interface *iface, struct Packet *p);

// opens the interface
// @returns false on error
typedef bool iface_open(struct Interface *iface);

// closes the interface, called by @close_interface
// @close_interface frees @name and @ifname so this callback doesn't have to
// @returns false on error
typedef bool iface_close(struct Interface *iface);

// @return a function that can read @property of the interface
// @type is the type the consumer wants
// the bitoffset and bitlength of the property should be set in @value
typedef value_producer *iface_get_property_reader(const struct Interface *iface, const char *property, enum ProtocolFieldType type, struct Value *value);

struct Interface {
    enum IfaceType type;
    char *name;
    char *ifname;
    int recvfd;
    // all of these callbacks are mandatory
    iface_recv *recv;
    iface_send *send;
    iface_open *open;
    iface_close *close_; // private TODO mark all private members
    iface_get_property_reader *get_property_reader;
    void *iface_private;
    unsigned reference_count;
    bool shutdown; // stop receiving when this is set

    struct ParseTree *parsetree;

    //TODO properties that actions can query: MAC address, IP address, port etc.
    //  TODO we need a per-type list of the names for the config validator
    //  TODO we need a per-instance hash to query the values

    //TODO we should do a refcounting of the interfaces
    //      pipelines hold a ref to the interfaces they send on
    //      iface is not deleted while we have a packet in the system that will be sent out on that iface
    //      if iface is marked for deletion (but still having ref) stops receiving packets
};

// no global init(), each interface type has its own

// TODO we should keep the interfaces in a hash, not an array

void iface_set_parsetree(struct Interface *iface, struct ParseTree *pt);

// closes the interface but doesn't free the given pointer (iface is in an array!)
// this just sets the shutdown state so it won't receive packets
// the interface will really close when no pipeline references it anymore
void close_iface(struct Interface *iface);

// add a reference to the interface
void iface_ref(struct Interface *iface);

// remove a reference from the interface
// the interface closes when shutdown=true AND refcount=0
void iface_unref(struct Interface *iface);


#endif // R2_INTERFACE_H
