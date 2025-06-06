// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_MAINTENANCE_H
#define R2_OAM_MAINTENANCE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "oam_request.h"

#include "json.h"
#include "packet.h"

struct OAM_MaintenancePoint;

const char *mp_type_to_str(enum OAM_MP_Type type);

const char *mp_get_name(const struct OAM_MaintenancePoint *mp);

enum OAM_MP_Type mp_get_type(const struct OAM_MaintenancePoint *mp);

struct JsonValue *mp_get_state_json(const struct OAM_MaintenancePoint *mp, int object_info);

// rewrites the header list in @p to reflect that this is an OAM message
// (OAM packets mimic the user data packets, but they have different format)
// @returns false if the original header list looks like it can't be an OAM packet
bool mp_reinterpret_oam_packet(struct OAM_MaintenancePoint *mp, struct Packet *p);

// maintain the packet counters for the received OAM messages
void mp_count_received_message(struct OAM_MaintenancePoint *mp, const struct Packet *p);

// decodes the contents of the OAM message in @packet according to the flavor of @mp
// @returns a JSON that contains all the information, or NULL on error
//  PW: adds the d-ACH contents to the JSON
//  TSN: adds level from CFM header to the JSON TODO what about RTAG?
struct JsonValue *mp_unpack_message(const struct OAM_MaintenancePoint *mp, const struct Packet *p);

// writes the payload part of @p from JSON in @msg according to the flavor of @mp
// the items that go into the fixed header are not serialized into the payload
// mp_reinterpret_oam_packet() must have been called on @p before this!
// @returns true on success
bool mp_update_message_payload(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct JsonValue *msg);

bool mp_pack_message(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct OamRequest *req);

// @returns level <=> mp->level
//  <0 means the @level is lower, the message must be deleted without processing
//  =0 means the levels match, the message must be processed
//  >0 means the @level is higher, the message must be forwarded without processing
int mp_compare_level(const struct OAM_MaintenancePoint *mp, unsigned level);

//TODO how do we prepare the packet?
void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *p);

#endif // R2_OAM_MAINTENANCE_H
