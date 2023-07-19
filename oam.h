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

struct Oam {
    struct Interface *if_oam_cmd;
    struct Interface *if_oam;
    int level;
    struct ConfObject *target; // PRF, PEF, POF, etc.
    char *name;
    unsigned pos_in_pipeline;
};

bool init_oam(struct R2d2Config *config);

int oam_ping(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_trace(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_discovery(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_send_reply(char *address, char *msg);

void oam_create_mep_start(const char *name, int level, unsigned idx);

void set_oam_cmd_if(struct Interface *iface);
void add_oam_if(struct Interface *iface);
struct Interface *get_oam_cmd_if(const char *name);
struct Interface *get_oam_if(const char *name);

#endif // R2_OAM_H
