// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include <stdbool.h>
#include "conf_object.h"
#include "configfile.h"
#include "if_oam_cmd.h"
#include "interface.h"

extern struct Interface *oam_cmd_iface;
extern int nr_oam_ifaces;
extern struct Interface *oam_ifaces[16];

struct Oam {
    struct Interface *if_oam_cmd;
    struct Interface *if_oam;
    int level;
    struct ConfObject *target; // PRF, PEF, POF, etc.
    char *name;
    unsigned pos_in_pipeline;
};

bool init_oam(struct R2d2Config *config);

int oam_ping(struct Interface *if_oam_cmd, unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_trace(struct Interface *if_oam_cmd, unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_discovery(struct Interface *if_oam_cmd, unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_send_reply(char *address, char *msg);

#endif // R2_OAM_H
