// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_INTERFACE_H
#define R2_INTERFACE_H

#include "pipeline.h"
#include "protocol.h"
#include "time_utils.h"
#include "value.h"

#include <stdio.h>
#include <stdbool.h>

struct Interface;

struct HeaderDescriptor;
struct ParseTree;

enum IfaceType {
    IF_ETH = 1,
    IF_INTERNAL,
    IF_IP,
    IF_OAM,
    IF_OAM_ETH,
    IF_OAM_CMD,
    IF_UDP_IN,
    IF_UDP_OUT,
};

enum IfaceState {
    IFS_INIT = 1, // created but not yet opened
    IFS_OPEN, // opened and ready to send/recv
    IFS_SHUTDOWN, // closing, can still send but not recv
};

// receive a packet on @fd
// blocks if no packet is in the rx queue!
// @returns true if the reception was successful
typedef bool iface_recv(struct Interface *iface);

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
// @type is the type the consumer wants, it is checked against the property
// the bitoffset and bitlength in @target is checked against the property
// @returns NULL on error
typedef value_producer *iface_get_property_reader(const struct Interface *iface, const char *property,
        enum ProtocolFieldType target_type, const struct Value *target);

struct Interface {
    enum IfaceType type;
    int reference_count;
    int sender_count;
    enum IfaceState state;
    int recvfd; // receives packets on this file descriptor
    char *name;
    char *ifname;
    void *iface_private;
    bool delay_offload;
    int dropstat_cntr;
    int dropstat_last_warn;

    unsigned long long recv_packets;
    unsigned long long recv_octets;
    unsigned long long send_packets;
    unsigned long long send_octets;

    struct timespec last_alert;

    struct ParseTree *parsetree_; // private

    // all of these methods are mandatory
    iface_recv *recv;
    iface_send *send;
    iface_open *open;
    iface_close *close_;

    // this method is optional
    iface_get_property_reader *get_property_reader;

    // this method is optional
    void (*print_private_info)(const struct Interface *iface, FILE *cmd_w);
};

// no global constructor, each interface type has its own, they all return Interface*

// add a reference to the interface, this is an ownership relation
// typically there is only one owner (hashmap countainer) that holds reference
void iface_ref(struct Interface *iface);

// remove a reference from the interface
// the interface disappears when reference_count=0 and sender_count=0
void iface_unref(struct Interface *iface);

// count the streams that are sending packets on @iface
// interface with @reference_count=0 can not disappear while it is still used for sending traffic
void iface_add_sender(struct Interface *iface);

// count the streams that are sending packets on @iface
// interface with @reference_count=0 can not disappear while it is still used for sending traffic
void iface_del_sender(struct Interface *iface);

// add a stream that this interface will receive
// adds a reference to @pipe, makes a copy of @headers
// @returns true on success
bool iface_add_stream(struct Interface *iface, struct HeaderDescriptor *headers, struct Pipeline *pipe);

// @returns the name of this interface @type
const char *iface_type_str(enum IfaceType type);

// prints information about @iface to @cmd_w
// for the 'iface' telnet command
void iface_print_info(const struct Interface *iface, FILE *cmd_w);

#endif // R2_INTERFACE_H
