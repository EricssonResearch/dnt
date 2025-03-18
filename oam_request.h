// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_REQUEST_H
#define R2_OAM_REQUEST_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdbool.h>

struct OamRequest;
struct MepStart;


// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct OamRequest *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        char *conn_name);

// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct OamRequest *parse_rping_command(const char *oam_command,
        char *conn_name);

// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct OamRequest *parse_trig_command(const char *oam_command,
        char *conn_name);

// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct OamRequest *parse_rlist_command(const char *oam_command,
        char *conn_name);

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_mask_command(const char *oam_command, char *conn_name);

struct OamRequest *delete_oam_request(struct OamRequest *req);

const char *request_get_type(const struct OamRequest *req);

// @error will be owned by the request
void request_set_error(struct OamRequest *req, char *error);

const char *request_get_error(const struct OamRequest *req);

const char *request_get_stream_name(const struct OamRequest *req);

const char *request_get_start_name(const struct OamRequest *req);

const char *request_get_stop_name(const struct OamRequest *req);

void request_set_mepstart(struct OamRequest *req, struct MepStart *start);

int request_get_level(const struct OamRequest *req);

void request_set_level(struct OamRequest *req, int level);

void request_set_count(struct OamRequest *req, unsigned count);

// @return_address will be owned by the request
void request_set_return(struct OamRequest *req, char *return_address, int return_port);

const char *request_get_return_ip(const struct OamRequest *req);

int request_get_return_port(const struct OamRequest *req);

// @stream will be owned by the request
void request_set_originator(struct OamRequest *req, char *stream, unsigned char session_id);

// returns true on success
bool initiate_request(struct OamRequest *req);


#endif // R2_OAM_REQUEST_H
