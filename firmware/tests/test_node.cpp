// System scenarios: the real battery and body node logic on a virtual bus (tests/sim_bus.hpp).
// The "[measured in sim]" lines are simulator timings, not hardware numbers.
#include <cstdio>
#include "unity.h"
#include "can_msgs.hpp"
#include "sim_bus.hpp"
#include "static_queue.hpp"

using namespace fleetbus;
using fleetbus::test::SimBus;

static SimBus* bus;
void setUp() { bus = new SimBus; }
void tearDown() { delete bus; }

static void test_nodes_boot_and_hear_each_other() {
    bus->run(2000);
    TEST_ASSERT_EQUAL(FB_ST_NORMAL, bus->battery.state()); TEST_ASSERT_EQUAL(FB_ST_NORMAL, bus->body.state());
    TEST_ASSERT_TRUE(bus->battery.peer_alive(bus->now)); TEST_ASSERT_TRUE(bus->body.peer_alive(bus->now));
    TEST_ASSERT_FALSE(bus->body.outputs().limp_home);
    TEST_ASSERT_EQUAL_UINT32(0, bus->battery.tx_dropped() + bus->body.tx_dropped());
}
static void test_body_waits_in_limp_home_until_it_hears_the_battery() {
    BodyNode lone{1}; lone.start(0);
    lone.tick(10);
    TEST_ASSERT_TRUE(lone.outputs().limp_home);        // never heard = dead: fail safe at power-up
}
static void test_heartbeat_ids_and_rates() {
    bus->run(1000);
    int hb_bat = 0, hb_body = 0, batt = 0, body = 0;
    for (const auto& f : bus->log) {
        if (f.id == heartbeat_id(FB_NODE_BATTERY)) hb_bat++;
        if (f.id == heartbeat_id(FB_NODE_BODY)) hb_body++;
        if (f.id == msg::kBatteryStatusId) batt++;
        if (f.id == msg::kBodyStatusId) body++;
    }
    TEST_ASSERT_EQUAL_UINT32(0x101, heartbeat_id(FB_NODE_BATTERY)); TEST_ASSERT_EQUAL_UINT32(0x102, heartbeat_id(FB_NODE_BODY));
    TEST_ASSERT_EQUAL(10, hb_bat); TEST_ASSERT_EQUAL(10, hb_body);   // 10 Hz each
    TEST_ASSERT_EQUAL(10, batt); TEST_ASSERT_EQUAL(10, body);
}
static void test_body_holds_latest_battery_status() {
    TEST_ASSERT_FALSE(bus->body.peer_battery().valid);
    bus->sample = BatterySample{3712, -842, 251, true};
    bus->run(300);
    const PeerBattery& b = bus->body.peer_battery();
    TEST_ASSERT_TRUE(b.valid);
    TEST_ASSERT_EQUAL_INT32(3712, b.pack_mv); TEST_ASSERT_EQUAL_INT32(-842, b.ma); TEST_ASSERT_EQUAL_INT32(251, b.temp_dc);
    TEST_ASSERT_EQUAL(BatteryNode::soc_pct(3712), b.soc_pct);
    TEST_ASSERT_TRUE(bus->now - b.at_ms <= 110);
    bus->battery_unplugged = true; bus->run(1000);
    TEST_ASSERT_TRUE(bus->now - bus->body.peer_battery().at_ms >= 1000);   // stale value ages: display shows SILENT
    TEST_ASSERT_FALSE(bus->body.gauge_inputs(bus->now).battery_alive);
}
static void test_overtemp_puts_body_in_limp_home() {
    bus->run(2500);
    bus->sample.temp_dc = 700;
    const uint32_t dt = bus->run_until([] { return bus->body.outputs().limp_home; }, 3000);
    TEST_ASSERT_TRUE(bus->body.outputs().limp_home);
    std::printf("    [measured in sim] overtemp -> body limp-home after %u ms\n", static_cast<unsigned>(dt));
    TEST_ASSERT_LESS_THAN(600, dt);                    // 3 debounced samples at 10 Hz + one tick
    TEST_ASSERT_EQUAL(FB_LIGHT_FAULT, bus->body.outputs().lights); TEST_ASSERT_EQUAL(90, bus->body.outputs().servo_deg);
}
static void test_fault_event_sent_on_entry_then_every_second() {
    bus->run(500); bus->log.clear();
    bus->sample.pack_mv = 2900;
    bus->run(3000);
    int events = 0;
    for (const auto& f : bus->log) if (f.id == msg::kFaultEventId) events++;
    TEST_ASSERT_TRUE(events >= 3 && events <= 4);
    TEST_ASSERT_EQUAL(FB_FAULT_UNDERVOLT, bus->battery.fault().code());
}
static void test_unplugged_battery_puts_body_in_limp_home_and_it_recovers() {
    bus->run(2500); TEST_ASSERT_FALSE(bus->body.outputs().limp_home);
    bus->battery_unplugged = true;
    const uint32_t dt = bus->run_until([] { return bus->body.outputs().limp_home; }, 3000);
    TEST_ASSERT_TRUE(bus->body.outputs().limp_home);
    std::printf("    [measured in sim] battery unplugged -> body limp-home after %u ms\n", static_cast<unsigned>(dt));
    TEST_ASSERT_LESS_OR_EQUAL(kPeerTimeoutMs + kHeartbeatPeriodMs + kTickMs, dt);
    bus->battery_unplugged = false;
    const uint32_t back = bus->run_until([] { return !bus->body.outputs().limp_home; }, 3000);
    std::printf("    [measured in sim] battery back -> body leaves limp-home after %u ms\n", static_cast<unsigned>(back));
    TEST_ASSERT_FALSE(bus->body.outputs().limp_home);
    TEST_ASSERT_TRUE(back >= kRecoverHoldMs);
}
static void test_tx_queue_drops_and_counts_when_full() {
    StaticQueue<int, 4> q;
    TEST_ASSERT_TRUE(q.push(1)); TEST_ASSERT_TRUE(q.push(2)); TEST_ASSERT_TRUE(q.push(3));
    TEST_ASSERT_FALSE(q.push(4));                       // N - 1 usable slots
    TEST_ASSERT_EQUAL_UINT32(1, q.dropped());
    int v = 0; TEST_ASSERT_TRUE(q.pop(v)); TEST_ASSERT_EQUAL(1, v);
    TEST_ASSERT_TRUE(q.push(5)); TEST_ASSERT_EQUAL(3u, q.size());
}
static void test_frames_with_unknown_protocol_version_are_ignored() {
    BodyNode b{1}; b.start(0);
    CanFrame f; f.id = heartbeat_id(FB_NODE_BATTERY); f.dlc = 8;
    msg::Heartbeat h; h.node_id = FB_NODE_BATTERY; h.state = FB_ST_NORMAL; h.proto_ver = 2;
    h.pack(f.data.data());
    b.on_can(5, f); b.tick(10);
    TEST_ASSERT_FALSE(b.peer_alive(10));
    TEST_ASSERT_TRUE(b.outputs().limp_home);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nodes_boot_and_hear_each_other); RUN_TEST(test_body_waits_in_limp_home_until_it_hears_the_battery);
    RUN_TEST(test_heartbeat_ids_and_rates); RUN_TEST(test_body_holds_latest_battery_status);
    RUN_TEST(test_overtemp_puts_body_in_limp_home); RUN_TEST(test_fault_event_sent_on_entry_then_every_second);
    RUN_TEST(test_unplugged_battery_puts_body_in_limp_home_and_it_recovers);
    RUN_TEST(test_tx_queue_drops_and_counts_when_full); RUN_TEST(test_frames_with_unknown_protocol_version_are_ignored);
    return UNITY_END();
}
