#pragma once
/* Values shared by the C board layer (bsp/), the C++ node logic (common/) and the DBC. Plain C so both can include it. */
#include <stdint.h>

#define FB_PROTO_VER 1u

typedef enum { FB_NODE_BATTERY = 1, FB_NODE_BODY = 2 } fb_node_id_t;

/* Matches VAL_ 256 State in can/fleetbus.dbc */
typedef enum {
    FB_ST_BOOT = 0, FB_ST_NORMAL = 1, FB_ST_WARNING = 2, FB_ST_FAULT = 3, FB_ST_SAFE = 4, FB_ST_LIMP_HOME = 5
} fb_state_t;

typedef enum {
    FB_FAULT_NONE = 0, FB_FAULT_OVERTEMP = 0x10, FB_FAULT_UNDERVOLT = 0x11,
    FB_FAULT_OVERCURRENT = 0x12, FB_FAULT_SENSOR = 0x13
} fb_fault_code_t;

/* Body node light outputs (BodyStatus.Lights bitmask; bsp_lights_set). */
#define FB_LIGHT_HEAD  0x01u
#define FB_LIGHT_TURN  0x02u
#define FB_LIGHT_FAULT 0x04u
