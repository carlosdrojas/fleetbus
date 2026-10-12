#pragma once
#include <stdint.h>
/* Peer heartbeat timeout detector (wrap-safe for 32-bit ms ticks). A peer never heard from counts as dead. */
typedef struct { uint32_t last_ms; uint32_t timeout_ms; int seen; } fb_hb_monitor_t;
void fb_hb_init(fb_hb_monitor_t *m, uint32_t timeout_ms);
void fb_hb_feed(fb_hb_monitor_t *m, uint32_t now_ms);
int  fb_hb_peer_alive(const fb_hb_monitor_t *m, uint32_t now_ms);
