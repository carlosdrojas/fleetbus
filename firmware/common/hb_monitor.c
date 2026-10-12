#include "hb_monitor.h"
void fb_hb_init(fb_hb_monitor_t *m, uint32_t timeout_ms) { m->last_ms = 0; m->timeout_ms = timeout_ms; m->seen = 0; }
void fb_hb_feed(fb_hb_monitor_t *m, uint32_t now_ms) { m->last_ms = now_ms; m->seen = 1; }
int  fb_hb_peer_alive(const fb_hb_monitor_t *m, uint32_t now_ms) {
    return m->seen && (uint32_t)(now_ms - m->last_ms) <= m->timeout_ms;
}
