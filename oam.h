// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include "configfile.h"
#include "interface.h"
#include "pipeline.h"
#include "seq_recov.h"

#include <stdio.h>
enum OamReqestMode {
    OAM_CLI = 1,
    OAM_CFG,
};

bool init_oam(struct R2d2Config *config);
bool close_oam(void);

int oam_send_reply(const char *address, unsigned port, const char *msg, unsigned msg_len);
int oam_recv_reply(char *msg);
int oam_command_loop(struct Interface *iface);

struct oam_request* oam_parse_ping(char *cmd_str, int mode, FILE *cmd_w);
int oam_start_ping(char *command, char *dest_ip, int dest_port, int mode, FILE *cmd_w);

int oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx);
void oam_set_pipeline_for_mep_start(const char *stream_name, struct Pipeline *pipe);

bool set_oam_cmd_if(struct Interface *iface);
void add_oam_if(struct Interface *iface);
unsigned short get_oam_nodeid(void);

/* Return a SeqRecovery for the given key, or create it if not exist.
 * The recommended key is session ID + node ID of the OAM packet
*/
struct SequenceRecovery *get_oam_rcvy(char *key);

/* Delete the SeqRecovery with the given key.
 * The temporary, OAM SeqRecoveries store this key as a member
 * */
void delete_oam_rcvy(char *key);

#endif // R2_OAM_H
