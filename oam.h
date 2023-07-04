// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_H
#define R2_OAM_H

#include <stdbool.h>
#include "configfile.h"

bool init_oam(struct R2d2Config *config);

int oam_ping(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_trace(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);
int oam_discovery(unsigned id, char *stream, char *mep_start, char *mep_stop, int level);

#endif // R2_OAM_H
