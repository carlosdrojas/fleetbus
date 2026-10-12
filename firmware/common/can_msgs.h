#pragma once
/* MOCK(M1): hand-written stand-in. M1 replaces this with code generated from can/fleetbus.dbc. */
#include <stdint.h>

#define FB_ID_HEARTBEAT 0x100u
#define FB_ID_BATTERY   0x200u

typedef struct { uint8_t node_id, proto_ver; uint16_t fw_version; uint32_t uptime_s; uint8_t state; } fb_heartbeat_t;

void fb_heartbeat_pack(const fb_heartbeat_t *m, uint8_t out[8]);
void fb_heartbeat_unpack(const uint8_t in[8], fb_heartbeat_t *m);
