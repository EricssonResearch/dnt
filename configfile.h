// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONFIGFILE_H
#define R2_CONFIGFILE_H

#include "state.h"

// reads the given file and creates a transaction
// @returns NULL on error
struct StateTransaction *read_config_file(const char *filename);


#endif // R2_CONFIGFILE_H
