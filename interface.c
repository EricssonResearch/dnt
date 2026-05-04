// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "interface.h"
#include "log.h"
#include "parsetree.h"
#include "pipeline.h"

#include <stdlib.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

static void delete_interface(struct Interface *iface)
{
    iface->close_(iface);
    delete_parsetree(iface->parsetree_);
    free(iface->name);
    free(iface->ifname);
    free(iface);
}

void iface_ref(struct Interface *iface)
{
    int refcount = __atomic_add_fetch(&iface->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s ref refcount %d senders %d", iface->name, refcount, iface->sender_count);
}

void iface_unref(struct Interface *iface)
{
    int refcount = __atomic_sub_fetch(&iface->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s unref refcount %d senders %d", iface->name, refcount, iface->sender_count);

    if (refcount == 0) {
        iface->parsetree_ = delete_parsetree(iface->parsetree_);
        if (iface->sender_count > 0) {
            iface->state = IFS_SHUTDOWN;
        } else {
            delete_interface(iface);
        }
    }
    //TODO if (refcount < 0) WTF
}

void iface_add_sender(struct Interface *iface)
{
    int sendercount = __atomic_add_fetch(&iface->sender_count, 1, __ATOMIC_RELAXED);
    log_debug("%s add sender refcount %d senders %d", iface->name, iface->reference_count, sendercount);
}

void iface_del_sender(struct Interface *iface)
{
    int sendercount = __atomic_sub_fetch(&iface->sender_count, 1, __ATOMIC_RELAXED);
    log_debug("%s del sender refcount %d senders %d", iface->name, iface->reference_count, sendercount);

    if (sendercount == 0) {
        if (iface->reference_count > 0) {
            // nothing
        } else {
            delete_interface(iface);
        }
    }
    //TODO if (sendercount < 0) WTF
}

bool iface_add_stream(struct Interface *iface, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    if (iface->parsetree_ == NULL) {
        iface->parsetree_ = new_parsetree(iface);
    }
    if (!parsetree_add_stream(iface->parsetree_, headers, pipe)) {
        log_error("failed to add stream %s to parsetree", pipe->name);
        return false;
    }
    pipeline_ref_send_interfaces(pipe);
    return true;
}

const char *iface_type_str(enum IfaceType type)
{
    switch (type) {
        case IF_ETH:
            return "Ethernet";
        case IF_INTERNAL:
            return "Internal";
        case IF_IP:
            return "IP";
        case IF_OAM:
            return "OAM Return";
        case IF_OAM_ETH:
            return "OAM ETH Return";
        case IF_OAM_CMD:
            return "OAM Command";
        case IF_UDP_IN:
            return "UDP In";
        case IF_UDP_OUT:
            return "UDP Out";
    }
    return NULL;
}

void iface_print_info(const struct Interface *iface, FILE *cmd_w, bool stream_info)
{
    static const char *state_names[] = { "\033[0mUNKNOWN", "\033[33mINIT", "\033[32mOPEN", "\033[31mSHUTDOWN" };
    fprintf(cmd_w, "iface \033[36m%s\033[0m type \033[1m%s\033[0m state %s\033[0m\n", iface->name,
            iface_type_str(iface->type), state_names[iface->state]);
    fprintf(cmd_w, "    recv %llu packets %llu octets\n    send %llu packets %llu octets\n",
            iface->recv_packets, iface->recv_octets, iface->send_packets, iface->send_octets);
    if (iface->checksum_errors)
        fprintf(cmd_w, "    checksum errors %llu\n", iface->checksum_errors);
    if (iface->dropstat_cntr)
        fprintf(cmd_w, "    dropstat %d\n", iface->dropstat_cntr);
    if (iface->print_private_info)
        iface->print_private_info(iface, cmd_w);
    if (stream_info) {
        fprintf(cmd_w, "    senders %d\n", iface->sender_count);
        if (iface->parsetree_) {
            parsetree_print_info(iface->parsetree_, cmd_w);
        }
    }
}
