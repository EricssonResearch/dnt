// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_MAINTENANCE_H
#define R2_OAM_MAINTENANCE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "json.h"
#include "packet.h"

struct OAM_MaintenancePoint;

const char *mp_type_to_str(enum OAM_MP_Type type);

enum OAM_MP_Type mp_get_type(struct OAM_MaintenancePoint *mp);

struct JsonValue *mp_get_state_json(struct OAM_MaintenancePoint *mp, int object_info);

//TODO how do we prepare the packet?
void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *packet);

#endif // R2_OAM_MAINTENANCE_H
