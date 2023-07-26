// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include "configfile.h"
#include "interface.h"
#include "pipeline.h"
#include "seq_recov.h"

bool init_oam(struct R2d2Config *config);

int oam_send_reply(const char *address, unsigned port, const char *msg, unsigned msg_len);
int oam_recv_reply(char *msg);
int oam_command_loop(int cmd_fd);

int oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx);
void oam_set_pipeline_for_mep_start(const char *stream_name, struct Pipeline *pipe);

void set_oam_cmd_if(struct Interface *iface);
void add_oam_if(struct Interface *iface);

/* Return a SeqRecovery for the given key, or create it if not exist.
 * The recommended key is session ID + node ID of the OAM packet
*/
struct SequenceRecovery *get_oam_rcvy(char *key);

/* Delete the SeqRecovery with the given key.
 * The temporary, OAM SeqRecoveries store this key as a member
 * */
void delete_oam_rcvy(char *key);

#endif // R2_OAM_H
