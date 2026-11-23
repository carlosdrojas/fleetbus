#pragma once
// Portable node logic: no hardware, no RTOS. The FreeRTOS node task (app/app_main.cpp) and the host simulator
// (host/fbsim.cpp) drive exactly this code with the same three calls:
//   on_can(now, frame)   every received CAN frame
//   tick(now, ...)       every kTickMs
//   pop_tx(frame)        drain what the node wants to send
// The role is a type, picked at compile time (BatteryNode or BodyNode): no virtual dispatch, no heap.
#include <array>
#include <cstdint>
#include "battery_fault.hpp"
#include "fb_types.h"
#include "gauge_view.hpp"
#include "heartbeat_monitor.hpp"
#include "limp_home.hpp"
#include "static_queue.hpp"

namespace fleetbus {

constexpr uint32_t kTickMs = 10;            // node task period
constexpr uint32_t kHeartbeatPeriodMs = 100;
constexpr uint32_t kStatusPeriodMs = 100;
constexpr uint32_t kFaultRepeatMs = 1000;   // FaultEvent repeat while latched
// 5 missed heartbeats. Long enough to ride out a few lost frames or one error-passive recovery, short enough that
// the body reacts within half a second. TARGET until measured on the bench.
constexpr uint32_t kPeerTimeoutMs = 500;
constexpr uint32_t kRecoverHoldMs = 1000;   // body: battery must look healthy this long before leaving limp-home

constexpr uint32_t kHeartbeatBaseId = 0x100;
constexpr uint32_t heartbeat_id(fb_node_id_t node) noexcept { return kHeartbeatBaseId + static_cast<uint32_t>(node); }
constexpr bool is_heartbeat_id(uint32_t id) noexcept { return (id & ~0xFu) == kHeartbeatBaseId; }
static_assert(heartbeat_id(FB_NODE_BATTERY) != heartbeat_id(FB_NODE_BODY), "nodes must never share an id");

struct CanFrame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    std::array<uint8_t, 8> data{};
};

// What every node does: heartbeats out, the peer's heartbeat in, a bounded TX queue.
class NodeBase {
public:
    static constexpr std::size_t kTxQueue = 16;

    [[nodiscard]] bool pop_tx(CanFrame& out) noexcept { return txq_.pop(out); }
    [[nodiscard]] fb_node_id_t id() const noexcept { return id_; }
    [[nodiscard]] fb_state_t state() const noexcept { return state_; }
    [[nodiscard]] bool peer_alive(uint32_t now_ms) const noexcept { return peer_.alive(now_ms); }
    [[nodiscard]] fb_state_t peer_state() const noexcept { return peer_state_; }
    [[nodiscard]] uint32_t tx_dropped() const noexcept { return txq_.dropped(); }

protected:
    constexpr NodeBase(fb_node_id_t id, fb_node_id_t peer, uint16_t fw_version) noexcept
        : id_(id), peer_id_(peer), fw_version_(fw_version) {}

    void start(uint32_t now_ms) noexcept;
    bool on_heartbeat(uint32_t now_ms, const CanFrame& f) noexcept;   // true if the frame was a heartbeat
    void heartbeat_if_due(uint32_t now_ms) noexcept;
    void send(uint32_t id, const uint8_t* data, uint8_t dlc) noexcept;
    static bool due(uint32_t now_ms, uint32_t last_ms, uint32_t period_ms, bool first) noexcept {
        return first || static_cast<uint32_t>(now_ms - last_ms) >= period_ms;
    }

    fb_state_t state_ = FB_ST_BOOT;

private:
    fb_node_id_t id_, peer_id_;
    uint16_t fw_version_;
    uint32_t boot_ms_ = 0, last_hb_ms_ = 0;
    bool hb_sent_ = false;
    HeartbeatMonitor peer_{kPeerTimeoutMs};
    fb_state_t peer_state_ = FB_ST_BOOT;
    StaticQueue<CanFrame, kTxQueue> txq_;
};

// Battery monitor ECU: runs the fault FSM on each sample, publishes BatteryStatus at 10 Hz and FaultEvent on a fault.
class BatteryNode : public NodeBase {
public:
    explicit constexpr BatteryNode(uint16_t fw_version) noexcept : NodeBase(FB_NODE_BATTERY, FB_NODE_BODY, fw_version) {}

    void start(uint32_t now_ms) noexcept { NodeBase::start(now_ms); }
    void on_can(uint32_t now_ms, const CanFrame& f) noexcept { on_heartbeat(now_ms, f); }
    void tick(uint32_t now_ms, const BatterySample& s) noexcept;

    [[nodiscard]] const BatteryFaultFsm& fault() const noexcept { return fault_; }
    [[nodiscard]] static uint8_t soc_pct(int32_t pack_mv) noexcept;   // linear 3.0-4.2 V placeholder curve

private:
    BatteryFaultFsm fault_;
    uint32_t last_status_ms_ = 0, last_fault_tx_ms_ = 0;
    bool status_sent_ = false;
};

// Latest BatteryStatus the body has received (for its display).
struct PeerBattery {
    int32_t pack_mv = 0, ma = 0, temp_dc = 0;
    uint8_t soc_pct = 0;
    uint32_t at_ms = 0;
    bool valid = false;
};

// Body controller ECU: lights and servo, limp-home when the battery faults or goes silent, the instrument cluster.
class BodyNode : public NodeBase {
public:
    explicit constexpr BodyNode(uint16_t fw_version) noexcept : NodeBase(FB_NODE_BODY, FB_NODE_BATTERY, fw_version) {}

    void start(uint32_t now_ms) noexcept { NodeBase::start(now_ms); }
    void on_can(uint32_t now_ms, const CanFrame& f) noexcept;
    void tick(uint32_t now_ms) noexcept;

    [[nodiscard]] const BodyOutputs& outputs() const noexcept { return out_; }
    [[nodiscard]] const PeerBattery& peer_battery() const noexcept { return batt_; }
    [[nodiscard]] GaugeInputs gauge_inputs(uint32_t now_ms) const noexcept;
    void command(uint8_t lights, uint8_t servo_deg) noexcept { fsm_.command(lights, servo_deg); }

private:
    LimpHomeFsm fsm_{kRecoverHoldMs};
    BodyOutputs out_;
    PeerBattery batt_;
    uint32_t last_status_ms_ = 0;
    bool status_sent_ = false;
};

}  // namespace fleetbus
