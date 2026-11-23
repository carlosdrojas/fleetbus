// The C ABI in fbsim.h over the C++ node classes. std::optional holds the node so power_on() can rebuild it from
// scratch, like a real reset; the role is fixed at sim_node_new().
#include "fbsim.h"
#include <cstring>
#include <new>
#include <optional>
#include "node.hpp"

using namespace fleetbus;

struct sim_node {
    int id;
    std::optional<BatteryNode> battery;
    std::optional<BodyNode> body;
};

extern "C" {

sim_node_t *sim_node_new(int node_id) {
    if (node_id != FB_NODE_BATTERY && node_id != FB_NODE_BODY) return nullptr;
    return new (std::nothrow) sim_node{node_id, {}, {}};
}
void sim_node_free(sim_node_t *n) { delete n; }

void sim_node_power_on(sim_node_t *n, uint32_t now_ms) {
    if (n->id == FB_NODE_BATTERY) { n->battery.emplace(1); n->battery->start(now_ms); }
    else { n->body.emplace(1); n->body->start(now_ms); }
}

void sim_node_rx(sim_node_t *n, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc) {
    CanFrame f;
    f.id = id;
    f.dlc = dlc > 8 ? 8 : dlc;
    std::memcpy(f.data.data(), data, f.dlc);
    if (n->battery) n->battery->on_can(now_ms, f);
    if (n->body) n->body->on_can(now_ms, f);
}

void sim_node_tick(sim_node_t *n, uint32_t now_ms, int32_t mv, int32_t ma, int32_t dc, int sensor_ok) {
    if (n->battery) n->battery->tick(now_ms, BatterySample{mv, ma, dc, sensor_ok != 0});
    if (n->body) n->body->tick(now_ms);
}

int sim_node_pop_tx(sim_node_t *n, uint32_t *id, uint8_t *data, uint8_t *dlc) {
    CanFrame f;
    const bool got = n->battery ? n->battery->pop_tx(f) : n->body ? n->body->pop_tx(f) : false;
    if (!got) return 0;
    *id = f.id; *dlc = f.dlc; std::memcpy(data, f.data.data(), 8);
    return 1;
}

int sim_node_state(const sim_node_t *n) {
    if (n->battery) return n->battery->state();
    if (n->body) return n->body->state();
    return -1;
}
uint8_t sim_node_lights(const sim_node_t *n) { return n->body ? n->body->outputs().lights : 0; }
uint8_t sim_node_servo(const sim_node_t *n) { return n->body ? n->body->outputs().servo_deg : 0; }
uint8_t sim_node_limp(const sim_node_t *n) { return n->body && n->body->outputs().limp_home ? 1 : 0; }

}  // extern "C"
