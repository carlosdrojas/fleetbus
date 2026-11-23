#pragma once
/* Host simulator of one FleetBus node: the real node logic (common/node.cpp), no hardware.
 * C ABI so Python can drive it through ctypes (dash/fleetbus/fbsim.py). NOT firmware; used for tests and demos. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sim_node sim_node_t;

sim_node_t *sim_node_new(int node_id);                 /* 1 = battery, 2 = body; NULL for anything else */
void sim_node_free(sim_node_t *n);
void sim_node_power_on(sim_node_t *n, uint32_t now_ms);  /* a fresh boot: all RAM state starts clean */
void sim_node_rx(sim_node_t *n, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc);
void sim_node_tick(sim_node_t *n, uint32_t now_ms, int32_t mv, int32_t ma, int32_t dc, int sensor_ok);
int  sim_node_pop_tx(sim_node_t *n, uint32_t *id, uint8_t *data, uint8_t *dlc);

int     sim_node_state(const sim_node_t *n);
uint8_t sim_node_lights(const sim_node_t *n);
uint8_t sim_node_servo(const sim_node_t *n);
uint8_t sim_node_limp(const sim_node_t *n);
#ifdef __cplusplus
}
#endif
