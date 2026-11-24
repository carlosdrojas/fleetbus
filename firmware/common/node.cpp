#include "node.hpp"
#include <cstring>
#include "can_msgs.hpp"

namespace fleetbus {

static_assert(msg::kHeartbeatBase == kHeartbeatBaseId, "heartbeat id moved");

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
    msg::Heartbeat h;
    if (!h.unpack(f.data.data(), f.dlc) || h.proto_ver != FB_PROTO_VER) return true;
    if (h.node_id == peer_id_) {
        peer_.feed(now_ms);
        peer_state_ = static_cast<fb_state_t>(h.state);
    }
    return true;
}

void NodeBase::heartbeat_if_due(uint32_t now_ms) noexcept {
    if (!due(now_ms, last_hb_ms_, kHeartbeatPeriodMs, !hb_sent_)) return;
    msg::Heartbeat h;
    h.node_id = static_cast<uint8_t>(id_);
    h.fw_version = fw_version_;
    h.uptime_s = (now_ms - boot_ms_) / 1000u;
    h.state = static_cast<uint8_t>(state_);
    h.proto_ver = FB_PROTO_VER;
    uint8_t b[8];
    h.pack(b);
    send(heartbeat_id(id_), b, msg::kHeartbeatDlc);
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

        msg::BatteryStatus b;
        b.pack_mv = static_cast<uint16_t>(s.pack_mv);
        b.ma = static_cast<int16_t>(s.ma);
        b.temp_dc = static_cast<int16_t>(s.temp_dc);
        b.soc_pct = soc_pct(s.pack_mv);
        b.proto_ver = FB_PROTO_VER;
        uint8_t buf[8];
        b.pack(buf);
        send(msg::kBatteryStatusId, buf, msg::kBatteryStatusDlc);

        if (fault_.latched() && (!was_latched || static_cast<uint32_t>(now_ms - last_fault_tx_ms_) >= kFaultRepeatMs)) {
            msg::FaultEvent fe;
            fe.code = static_cast<uint16_t>(fault_.code());
            fe.severity = static_cast<uint8_t>(fault_.state() == FB_ST_SAFE ? 3 : 2);
            fe.node_id = static_cast<uint8_t>(id());
            fe.proto_ver = FB_PROTO_VER;
            fe.pack(buf);
            send(msg::kFaultEventId, buf, msg::kFaultEventDlc);
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
    if (f.id == msg::kBatteryStatusId) {
        msg::BatteryStatus b;
        if (!b.unpack(f.data.data(), f.dlc) || b.proto_ver != FB_PROTO_VER) return;
        batt_.pack_mv = b.pack_mv;
        batt_.ma = b.ma;
        batt_.temp_dc = b.temp_dc;
        batt_.soc_pct = b.soc_pct;
        batt_.at_ms = now_ms;
        batt_.valid = true;
    }
}

void BodyNode::tick(uint32_t now_ms) noexcept {
    out_ = fsm_.step(now_ms, peer_alive(now_ms), peer_state());   // every 10 ms: limp-home latency is one tick
    state_ = fsm_.state();
    if (due(now_ms, last_status_ms_, kStatusPeriodMs, !status_sent_)) {
        msg::BodyStatus bs;
        bs.lights = out_.lights;
        bs.servo_deg = out_.servo_deg;
        bs.limp_home = out_.limp_home ? 1 : 0;
        bs.proto_ver = FB_PROTO_VER;
        uint8_t buf[8];
        bs.pack(buf);
        send(msg::kBodyStatusId, buf, msg::kBodyStatusDlc);
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
