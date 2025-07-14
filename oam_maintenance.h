// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_MAINTENANCE_H
#define R2_OAM_MAINTENANCE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "json.h"
#include "oam.h"
#include "packet.h"

#include <stdio.h>

#define MASK_PERIOD_MS 1000
#define MASK_TIMEOUT_MS 2500

struct OamRequest;

// @returns a pointer and adds a reference to the MP
// use @oam_unref_maintenance_point to release the reference
struct OAM_MaintenancePoint *find_maintenance_point(const char *name);

const char *mp_get_name(const struct OAM_MaintenancePoint *mp);

const char *mp_get_stream_name(const struct OAM_MaintenancePoint *mp);

unsigned mp_get_level(const struct OAM_MaintenancePoint *mp);

enum OAM_MP_Type mp_get_type(const struct OAM_MaintenancePoint *mp);

// @returns true if @mp can inject OAM packets into a pipeline
bool mp_can_send(const struct OAM_MaintenancePoint *mp);

// @returns a JSON object that describes the state of @mp
// adds detailed information about the linked pipeline object if @object_info is true
struct JsonValue *mp_get_state_json(const struct OAM_MaintenancePoint *mp, bool object_info);

// @returns an array of JSON objects for all maintenance points that are linked to the same object as @mp
struct JsonValue *mp_get_state_json_by_object(const struct OAM_MaintenancePoint *mp);

// prints information about @mp to @out
// without @details it just prints the name and the stream information
void mp_print_info(const struct OAM_MaintenancePoint *mp, FILE *out, bool details);

// prints masking information about @mp to @out
void mp_print_mask_signalling_state(const struct OAM_MaintenancePoint *mp, FILE *out);

// calls @cb for each mp in the system
// @userdata is supplied to @cb
int foreach_mp(bool sorted, hashmap_cb *cb, void *userdata);

// maintain the packet counters for the received OAM messages
void mp_count_received_message(struct OAM_MaintenancePoint *mp, const struct Packet *p);

// decodes the contents of the OAM message in @packet according to the encap of @mp
// @returns a JSON that contains all the information, or NULL on error
// doesn't expect mp_reinterpret_oam_packet to have been called on @p
//  PW: adds the d-ACH contents to the JSON
//  TSN: adds data from CFM, RTAG, ETH headers to the JSON
struct JsonValue *mp_unpack_message(const struct OAM_MaintenancePoint *mp, const struct Packet *p);

// initializes @p for sending @req in-band according to the encap of @mp
// @returns a JSON object that contains the essential information it needs to contain
// sets the timestamp of @p, adds it to the returned JSON too
// adds type and code="request" to the JSON, but none of the type-dependent information
// after this the caller should use @mp_pack_message_payload to set the payload of the message
struct JsonValue *mp_pack_message_header(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct OamRequest *req);

// writes the payload part of @p from JSON in @msg according to the encap of @mp
// the items that go into the fixed headers are not serialized into the payload
// @returns true on success
bool mp_pack_message_payload(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct JsonValue *msg);

// @returns p->level <=> mp->level
//  <0 means the p->level is lower, the message must be deleted without processing
//  =0 means the levels match, the message must be processed
//  >0 means the p->level is higher, the message must be forwarded without processing
int mp_compare_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p);

// @returns the ttl of @p (it is not simply p->ttl)
unsigned char mp_get_ttl(const struct OAM_MaintenancePoint *mp, const struct Packet *p);

// if @mp is an injection point, it launches @p on the pipeline
void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *p);

// start sending mask requests periodically in-band
// reports results to @cmd_w if it's not NULL
// @returns false if it can't send
bool mp_initiate_mask_signalling(struct OAM_MaintenancePoint *mp, FILE *cmd_w);

// stops sending mask requests, and sends one unmask request in-band
// reports results to @cmd_w if it's not NULL
// @returns false if it can't send
bool mp_stop_mask_signalling(struct OAM_MaintenancePoint *mp, FILE *cmd_w);

// receive a mask signal on @mp
// @mp should be a pre-automip of a SequenceRecovery object
// this will set the incoming branch of the recovery as masked
void mp_receive_mask_signal(struct OAM_MaintenancePoint *mp);

// receive an unmask signal on @mp
// @mp should be a pre-automip of a SequenceRecovery object
// this will set the incoming branch of the recovery as not masked
void mp_receive_unmask_signal(struct OAM_MaintenancePoint *mp);

#endif // R2_OAM_MAINTENANCE_H
