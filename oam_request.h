// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_REQUEST_H
#define R2_OAM_REQUEST_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "json.h"
#include "oam_maintenance.h"

#include <stdbool.h>

struct OamRequest;

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_rping_command(const char *oam_command);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_trigger_command(const char *oam_command, bool allow_num);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_rlist_command(const char *oam_command);

// always returns NULL
struct OamRequest *delete_oam_request(struct OamRequest *req);

struct OamRequest *create_mask_request(struct OAM_MaintenancePoint *mp, const char *type);

// always returns NULL
struct OamRequest *delete_mask_request(struct OamRequest *req);


const char *request_get_start_name(const struct OamRequest *req);

const char *request_get_stop_name(const struct OamRequest *req);

const char *request_get_stream_name(const struct OamRequest *req);

const char *request_get_type(const struct OamRequest *req);

const char *request_get_error(const struct OamRequest *req);

unsigned request_get_session_id(const struct OamRequest *req);

int request_get_level(const struct OamRequest *req);

int request_is_infinite(const struct OamRequest *req);

// @returns a newly allocated string formatted like
//      "[reply to ip %s port %u]"
//      "[reply to mac %s vlan %u]"
char *request_get_return_addr_string(const struct OamRequest *req);

// retreives the parameters that will be used in the packet to identify it
// this function is the get-all-data function for @mp_pack_message
void request_get_identification_data(const struct OamRequest *req, unsigned *nodeid,
        unsigned char *level, unsigned char *session,
        unsigned char *seq, unsigned char *ttl);


// @error will be owned by the request
void request_set_error(struct OamRequest *req, char *error);

void request_set_infinite_count(struct OamRequest *req);

void request_set_return_addr(struct OamRequest *req, struct JsonValue *addr);

void request_set_originator(struct OamRequest *req, const char *stream, unsigned char session_id);

// @returns true on success
// @conn_name is the name of the command connection where the request comes from, can be NULL
bool initiate_request(struct OamRequest *req, const char *conn_name);


#endif // R2_OAM_REQUEST_H
