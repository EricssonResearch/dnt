// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_CHECKSUM_H
#define DNT_CHECKSUM_H

#include "packet.h"
#include "protocol.h"

struct ChecksumParameters {
    unsigned hdr_idx;
    enum ProtocolID hdr_proto;
    unsigned ip_idx;
    enum ProtocolID ip_version;
};

// computes and writes checksum into the header prescribed in @cp
void checksum_compute(struct Packet *p, struct ChecksumParameters *cp);

// @returns true if the checksum of the header prescribed in @cp is correct
bool checksum_verify(struct Packet *p, struct ChecksumParameters *cp);

#endif // DNT_CHECKSUM_H
