// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_REQUEST_H
#define R2_OAM_REQUEST_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdbool.h>

struct OamRequest;
struct OAM_MaintenancePoint;

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        const char *conn_name);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_rping_command(const char *oam_command,
        const char *conn_name);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_trigger_command(const char *oam_command, bool allow_num,
        const char *conn_name);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_rlist_command(const char *oam_command,
        const char *conn_name);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_mask_command(const char *oam_command,
        const char *conn_name);

// always returns NULL
struct OamRequest *delete_oam_request(struct OamRequest *req);


const char *request_get_type(const struct OamRequest *req);

// @error will be owned by the request
void request_set_error(struct OamRequest *req, char *error);

const char *request_get_error(const struct OamRequest *req);

const char *request_get_stream_name(const struct OamRequest *req);

unsigned request_get_session_id(const struct OamRequest *req);

const char *request_get_start_name(const struct OamRequest *req);

const char *request_get_stop_name(const struct OamRequest *req);

void request_set_start(struct OamRequest *req, struct OAM_MaintenancePoint *start);

int request_get_level(const struct OamRequest *req);

void request_set_level(struct OamRequest *req, int level);

void request_set_count(struct OamRequest *req, unsigned count);

void request_set_return_addr(struct OamRequest *req, struct JsonValue *addr);

// @returns a newly allocated string formatted like
//      "[reply to ip %s port %u]"
//      "[reply to mac %s vlan %u]"
char *request_get_return_addr_string(struct OamRequest *req);

// @return_address will be owned by the request
void request_set_return(struct OamRequest *req, char *return_address, int return_port); //TODO delete

const char *request_get_return_ip(const struct OamRequest *req); //TODO delete

int request_get_return_port(const struct OamRequest *req); //TODO delete

void request_set_originator(struct OamRequest *req, const char *stream, unsigned char session_id);

// returns true on success
bool initiate_request(struct OamRequest *req);


#endif // R2_OAM_REQUEST_H
