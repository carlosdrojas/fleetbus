#include "node.hpp"
#include <cstring>
#include "can_msgs.h"    // generated from can/fleetbus.dbc (C)

namespace fleetbus {

static_assert(CAN_MSGS_HEARTBEAT_FRAME_ID == kHeartbeatBaseId, "DBC heartbeat id moved");

// ---------------------------------------------------------------------------------------------- NodeBase
void NodeBase::start(uint32_t now_ms) noexcept {
    boot_ms_ = now_ms;
    state_ = FB_ST_NORMAL;
}

void NodeBase::send(uint32_t id, const uint8_t* data, uint8_t dlc) noexcept {
    CanFrame f;
    f.id = id;
    f.dlc = dlc;
    std::memcpy(f.data.data(), data, dlc);
    (void)txq_.push(f);          // full queue: dropped and counted (tx_dropped), never blocks the 10 ms task
}

bool NodeBase::on_heartbeat(uint32_t now_ms, const CanFrame& f) noexcept {
    if (!is_heartbeat_id(f.id)) return false;
    can_msgs_heartbeat_t h;
    if (can_msgs_heartbeat_unpack(&h, f.data.data(), f.dlc) != 0 || h.proto_ver != FB_PROTO_VER) return true;
    if (h.node_id == peer_id_) {
        peer_.feed(now_ms);
        peer_state_ = static_cast<fb_state_t>(h.state);
    }
    return true;
}

void NodeBase::heartbeat_if_due(uint32_t now_ms) noexcept {
    if (!due(now_ms, last_hb_ms_, kHeartbeatPeriodMs, !hb_sent_)) return;
    can_msgs_heartbeat_t h{};
    h.node_id = static_cast<uint8_t>(id_);
    h.fw_version = fw_version_;
    h.uptime_s = (now_ms - boot_ms_) / 1000u;
    h.state = static_cast<uint8_t>(state_);
    h.proto_ver = FB_PROTO_VER;
    uint8_t b[8];
    can_msgs_heartbeat_pack(b, &h, sizeof b);
    send(heartbeat_id(id_), b, 8);
    last_hb_ms_ = now_ms;
    hb_sent_ = true;
}

// ---------------------------------------------------------------------------------------------- BatteryNode
uint8_t BatteryNode::soc_pct(int32_t mv) noexcept {
    if (mv >= 4200) return 100;
    if (mv <= 3000) return 0;
    return static_cast<uint8_t>((mv - 3000) * 100 / 1200);
}

void BatteryNode::tick(uint32_t now_ms, const BatterySample& s) noexcept {
    if (due(now_ms, last_status_ms_, kStatusPeriodMs, !status_sent_)) {
        const bool was_latched = fault_.latched();
        fault_.step(s);                                     // the FSM runs at the status rate: debounce = 3 x 100 ms
        state_ = fault_.state();

        can_msgs_battery_status_t b{};
        b.pack_milli_volt = static_cast<uint16_t>(s.pack_mv);
        b.current_milli_amp = static_cast<int16_t>(s.ma);
        b.temp_deci_c = static_cast<int16_t>(s.temp_dc);
        b.soc_pct = soc_pct(s.pack_mv);
        b.proto_ver = FB_PROTO_VER;
        uint8_t buf[8];
        can_msgs_battery_status_pack(buf, &b, sizeof buf);
        send(CAN_MSGS_BATTERY_STATUS_FRAME_ID, buf, 8);

        if (fault_.latched() && (!was_latched || static_cast<uint32_t>(now_ms - last_fault_tx_ms_) >= kFaultRepeatMs)) {
            can_msgs_fault_event_t fe{};
            fe.fault_code = static_cast<uint16_t>(fault_.code());
            fe.severity = static_cast<uint8_t>(fault_.state() == FB_ST_SAFE ? 3 : 2);
            fe.node_id = static_cast<uint8_t>(id());
            fe.proto_ver = FB_PROTO_VER;
            can_msgs_fault_event_pack(buf, &fe, sizeof buf);
            send(CAN_MSGS_FAULT_EVENT_FRAME_ID, buf, 8);
            last_fault_tx_ms_ = now_ms;
        }
        last_status_ms_ = now_ms;
        status_sent_ = true;
    }
    heartbeat_if_due(now_ms);
}

// ---------------------------------------------------------------------------------------------- BodyNode
void BodyNode::on_can(uint32_t now_ms, const CanFrame& f) noexcept {
    if (on_heartbeat(now_ms, f)) return;
    if (f.id == CAN_MSGS_BATTERY_STATUS_FRAME_ID) {
        can_msgs_battery_status_t b;
        if (can_msgs_battery_status_unpack(&b, f.data.data(), f.dlc) != 0 || b.proto_ver != FB_PROTO_VER) return;
        batt_.pack_mv = b.pack_milli_volt;
        batt_.ma = b.current_milli_amp;
        batt_.temp_dc = b.temp_deci_c;
        batt_.soc_pct = b.soc_pct;
        batt_.at_ms = now_ms;
        batt_.valid = true;
    }
}

void BodyNode::tick(uint32_t now_ms) noexcept {
    out_ = fsm_.step(now_ms, peer_alive(now_ms), peer_state());   // every 10 ms: limp-home latency is one tick
    state_ = fsm_.state();
    if (due(now_ms, last_status_ms_, kStatusPeriodMs, !status_sent_)) {
        can_msgs_body_status_t bs{};
        bs.lights = out_.lights;
        bs.servo_deg = out_.servo_deg;
        bs.limp_home = out_.limp_home ? 1 : 0;
        bs.proto_ver = FB_PROTO_VER;
        uint8_t buf[8];
        can_msgs_body_status_pack(buf, &bs, sizeof buf);
        send(CAN_MSGS_BODY_STATUS_FRAME_ID, buf, 4);
        last_status_ms_ = now_ms;
        status_sent_ = true;
    }
    heartbeat_if_due(now_ms);
}

GaugeInputs BodyNode::gauge_inputs(uint32_t now_ms) const noexcept {
    GaugeInputs g;
    g.have = batt_.valid;
    g.pack_mv = batt_.pack_mv; g.ma = batt_.ma; g.temp_dc = batt_.temp_dc; g.soc_pct = batt_.soc_pct;
    g.battery_alive = peer_alive(now_ms);
    g.battery_state = peer_state();
    g.limp_home = out_.limp_home;
    return g;
}

}  // namespace fleetbus
