#include <stdlib.h>
#include <string.h>
#include "fbsim.h"
#include "node_core.h"
#include "boot_logic.h"
#include "image.h"
#include "uart_link.h"
#include "../tests/fake_flash.h"

/* Image body byte 0 selects simulated app behaviour (bench-only fault injection; real boards use a separate
 * deliberately-broken build): 0 healthy · 1 hangs at boot (watchdog reset loop) · 2 runs but never becomes healthy. */
#define BEHAVE_OK 0
#define BEHAVE_HANG 1
#define BEHAVE_UNHEALTHY 2
#define SIM_WDG_RESET_MS 2000u

struct sim_node {
    int id;
    uint8_t pk[32];
    fake_flash_t ff; fb_flash_t fl;
    fb_node_t node;
    int alive, hung;
    uint32_t hung_since, boots, wdg_resets;
    int running_slot; uint32_t image_version; int behavior;
    uint32_t next_tick_ms;
    uint32_t reboot_at;          /* 0 = none. The real app waits 100 ms so the DONE ack can leave the TX FIFO */
};

sim_node_t *sim_node_new(int node_id, const uint8_t pubkey[32]) {
    sim_node_t *n = calloc(1, sizeof *n);
    n->id = node_id; memcpy(n->pk, pubkey, 32);
    fake_flash_init(&n->ff, &n->fl);
    return n;
}
void sim_node_free(sim_node_t *n) { free(n); }

int sim_node_provision(sim_node_t *n, int slot, const uint8_t *blob, uint32_t len) {
    if (slot < 0 || slot > 1 || len > FB_SLOT_SIZE) return -1;
    memcpy(n->ff.mem + (FB_SLOT_ADDR(slot) - FB_FLASH_BASE), blob, len);
    fb_boot_meta_t m; fb_meta_load(&n->fl, &m);
    uint32_t ver = 0;
    if (fb_image_verify(&n->fl, slot, (uint8_t)n->id, 0, n->pk, &ver) != FB_IMG_OK) return -2;
    m.status[slot] = FB_SLOT_CONFIRMED; m.version[slot] = ver; m.active = (uint8_t)slot;
    if (ver > m.min_version) m.min_version = ver;
    return fb_meta_store(&n->fl, &m);
}

void sim_node_power_on(sim_node_t *n, uint32_t now_ms) {
    fb_boot_result_t r;
    n->boots++; n->hung = 0; n->alive = 0; n->reboot_at = 0;
    if (fb_boot_run(&n->fl, (uint8_t)n->id, n->pk, &r) != 0) return;   /* stays in bootloader */
    n->running_slot = r.slot; n->image_version = r.version;
    uint8_t b0 = n->ff.mem[FB_SLOT_ADDR(r.slot) + FB_IMG_HDR_SIZE - FB_FLASH_BASE];
    n->behavior = b0 <= 2 ? b0 : BEHAVE_OK;
    fb_node_init(&n->node, (fb_node_id_t)n->id, (uint16_t)r.version, r.version, r.slot, &n->fl, n->pk, r.trial);
    n->node.boot_ms = now_ms ? now_ms : 1; n->node.state = FB_ST_NORMAL;
    n->alive = 1; n->next_tick_ms = now_ms;
    if (n->behavior == BEHAVE_HANG) { n->hung = 1; n->hung_since = now_ms; n->alive = 0; }
    if (n->behavior == BEHAVE_UNHEALTHY) n->node.tasks_ok = 0;
}

void sim_node_rx(sim_node_t *n, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (n->alive) fb_node_on_can(&n->node, now_ms, id, data, dlc);
}

void sim_node_tick(sim_node_t *n, uint32_t now_ms, int32_t mv, int32_t ma, int32_t dc, int sensor_ok) {
    if (n->hung) {                                   /* IWDG fires if the app never feeds it */
        if (now_ms - n->hung_since >= SIM_WDG_RESET_MS) { n->wdg_resets++; sim_node_power_on(n, now_ms); }
        return;
    }
    if (!n->alive) return;
    fb_sensors_t s = { mv, ma, dc, sensor_ok };
    fb_node_tick(&n->node, now_ms, &s);
    if (n->node.reboot_requested && !n->reboot_at) n->reboot_at = now_ms + 100;
    if (n->node.health_deadline_missed) { n->wdg_resets++; n->reboot_at = 0; sim_node_power_on(n, now_ms); return; }
    if (n->reboot_at && now_ms >= n->reboot_at) { n->reboot_at = 0; sim_node_power_on(n, now_ms); }
}

int sim_node_pop_tx(sim_node_t *n, uint32_t *id, uint8_t *data, uint8_t *dlc) {
    fb_can_frame_t f;
    if (!n->alive || !fb_node_pop_tx(&n->node, &f)) return 0;
    *id = f.id; *dlc = f.dlc; memcpy(data, f.data, 8);
    return 1;
}

int sim_node_alive(const sim_node_t *n) { return n->alive; }
int sim_node_state(const sim_node_t *n) { return n->alive ? (int)n->node.state : -1; }
int sim_node_slot(const sim_node_t *n) { return n->running_slot; }
uint32_t sim_node_image_version(const sim_node_t *n) { return n->image_version; }
int sim_node_slot_status(sim_node_t *n, int slot) { fb_boot_meta_t m; fb_meta_load(&n->fl, &m); return m.status[slot]; }
uint32_t sim_node_min_version(sim_node_t *n) { fb_boot_meta_t m; fb_meta_load(&n->fl, &m); return m.min_version; }
uint8_t sim_node_lights(const sim_node_t *n) { return n->node.act.lights; }
uint8_t sim_node_servo(const sim_node_t *n) { return n->node.act.servo_deg; }
uint8_t sim_node_limp(const sim_node_t *n) { return n->node.act.limp_home; }
uint32_t sim_node_boots(const sim_node_t *n) { return n->boots; }
uint32_t sim_node_resets_by_wdg(const sim_node_t *n) { return n->wdg_resets; }
void sim_node_flip_byte(sim_node_t *n, int slot, uint32_t off) { n->ff.mem[FB_SLOT_ADDR(slot) - FB_FLASH_BASE + off] ^= 0xFF; }

size_t sim_link_encode(uint32_t id, const uint8_t *data, uint8_t dlc, uint8_t *out) { return fb_link_encode(id, data, dlc, out); }
uint8_t sim_crc8(const uint8_t *d, size_t n) { return fb_crc8(d, n); }
