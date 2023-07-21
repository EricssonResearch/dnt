// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include <stdbool.h>
#include "conf_object.h"
#include "configfile.h"
#include "if_oam_cmd.h"
#include "interface.h"
#include "pipeline.h"
#include "seq_recov.h"

struct Oam {
    struct Interface *if_oam_cmd;
    struct Interface *if_oam;
    int level;
    struct ConfObject *target; // PRF, PEF, POF, etc.
    char *name;
};

bool init_oam(struct R2d2Config *config);

int oam_ping(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_trace(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_discovery(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_send_reply(char *address, char *msg);
int oam_recv_reply(char *msg);
int oam_command_loop(int cmd_fd);

void oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx);
void oam_set_pipeline_for_mep_start(const char *stream_name, struct Pipeline *pipe);

void set_oam_cmd_if(struct Interface *iface);
void add_oam_if(struct Interface *iface);
struct Interface *get_oam_cmd_if(const char *name);
struct Interface *get_oam_if(const char *name);

struct SequenceRecovery *get_oam_rcvy(char *session_id);
void delete_oam_rcvy(char *session_id);

#endif // R2_OAM_H
