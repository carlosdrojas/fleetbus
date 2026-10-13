#pragma once
/* Host simulator of one FleetBus node: real node_core + real boot logic + fake flash.
 * C ABI so Python can drive it through ctypes (gateway --sim, hil/). NOT firmware; used for tests and demos. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sim_node sim_node_t;

sim_node_t *sim_node_new(int node_id, const uint8_t pubkey[32]);
void sim_node_free(sim_node_t *n);
/* Program an image blob (header+body) into a slot and mark it CONFIRMED (factory provisioning). */
int  sim_node_provision(sim_node_t *n, int slot, const uint8_t *blob, uint32_t len);
void sim_node_power_on(sim_node_t *n, uint32_t now_ms);
void sim_node_rx(sim_node_t *n, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc);
void sim_node_tick(sim_node_t *n, uint32_t now_ms, int32_t mv, int32_t ma, int32_t dc, int sensor_ok);
int  sim_node_pop_tx(sim_node_t *n, uint32_t *id, uint8_t *data, uint8_t *dlc);

int  sim_node_alive(const sim_node_t *n);          /* 0 = bootloader-only or hung */
int  sim_node_state(const sim_node_t *n);
int  sim_node_slot(const sim_node_t *n);
uint32_t sim_node_image_version(const sim_node_t *n);
int  sim_node_slot_status(sim_node_t *n, int slot);
uint32_t sim_node_min_version(sim_node_t *n);
uint8_t sim_node_lights(const sim_node_t *n);
uint8_t sim_node_servo(const sim_node_t *n);
uint8_t sim_node_limp(const sim_node_t *n);
uint32_t sim_node_boots(const sim_node_t *n);
uint32_t sim_node_resets_by_wdg(const sim_node_t *n);
/* Body node only: latest BatteryStatus it received. out = {pack_mv, ma, temp_dc, soc_pct, age_ms}; returns 1 if valid. */
int  sim_node_peer_battery(const sim_node_t *n, uint32_t now_ms, int32_t out[5]);
/* Corrupt one byte of a slot (test hook for flash corruption). */
void sim_node_flip_byte(sim_node_t *n, int slot, uint32_t offset);
/* serial-link codec exposed for cross-language tests */
size_t sim_link_encode(uint32_t id, const uint8_t *data, uint8_t dlc, uint8_t *out);
uint8_t sim_crc8(const uint8_t *d, size_t n);
#ifdef __cplusplus
}
#endif
