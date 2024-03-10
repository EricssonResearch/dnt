// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_REQUEST_H
#define R2_OAM_REQUEST_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdbool.h>

struct oam_request;


// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct oam_request *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        char *conn_name);

// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct oam_request *parse_rping_command(const char *oam_command,
        char *conn_name);

// always returns a request, sets ret->error to an error message
// @conn_name will be owned by the request
struct oam_request *parse_rlist_command(const char *oam_command,
        char *conn_name);

struct oam_request *delete_oam_request(struct oam_request *req);

const char *request_get_type(const struct oam_request *req);

// @error will be owned by the request
void request_set_error(struct oam_request *req, char *error);

const char *request_get_error(const struct oam_request *req);

const char *request_get_stream_name(const struct oam_request *req);

const char *request_get_start_name(const struct oam_request *req);

const char *request_get_stop_name(const struct oam_request *req);

int request_get_level(const struct oam_request *req);

void request_override_count(struct oam_request *req, unsigned count);

// @return_address will be owned by the request
void request_set_return(struct oam_request *req, char *return_address, int return_port);

const char *request_get_return_ip(const struct oam_request *req);

int request_get_return_port(const struct oam_request *req);

// @stream will be owned by the request
void request_set_originator(struct oam_request *req, char *stream, unsigned char session_id);

// returns true on success
bool initiate_request(struct oam_request *req);


#endif // R2_OAM_REQUEST_H
