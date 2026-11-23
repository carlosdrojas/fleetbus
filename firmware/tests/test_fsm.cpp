// Battery fault FSM, limp-home FSM and heartbeat monitor, each on its own.
#include "unity.h"
#include "battery_fault.hpp"
#include "heartbeat_monitor.hpp"
#include "limp_home.hpp"

using namespace fleetbus;

static BatteryFaultFsm f;
void setUp() { f = BatteryFaultFsm{}; }
void tearDown() {}
static void steps(int n, int32_t mv, int32_t ma, int32_t dc, bool ok = true) {
    for (int i = 0; i < n; i++) f.step(BatterySample{mv, ma, dc, ok});
}

static void test_normal_stays_normal() { steps(100, 3800, 500, 250); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state()); }
static void test_warning_needs_debounce() {
    steps(2, 3800, 0, 500); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state());
    steps(1, 3800, 0, 500); TEST_ASSERT_EQUAL(FB_ST_WARNING, f.state());
}
static void test_single_glitch_ignored() {
    steps(2, 3800, 0, 500); steps(1, 3800, 0, 250); steps(2, 3800, 0, 500);
    TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state());
}
static void test_warning_clears_with_hysteresis() {
    steps(3, 3800, 0, 500); TEST_ASSERT_EQUAL(FB_ST_WARNING, f.state());
    steps(5, 3800, 0, 440); TEST_ASSERT_EQUAL(FB_ST_WARNING, f.state());   // inside the hysteresis band
    steps(3, 3800, 0, 400); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state());
}
static void test_overtemp_fault_then_safe() {
    steps(3, 3800, 0, 650); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state()); TEST_ASSERT_EQUAL(FB_FAULT_OVERTEMP, f.code());
    steps(10, 3800, 0, 650); TEST_ASSERT_EQUAL(FB_ST_SAFE, f.state());
}
static void test_fault_latches_when_cleared() {
    steps(3, 2900, 0, 250); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state());
    steps(5, 3800, 0, 250); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state());
    f.reset(); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state());
}
static void test_undervolt_and_overcurrent_and_sensor() {
    steps(3, 2900, 0, 250); TEST_ASSERT_EQUAL(FB_FAULT_UNDERVOLT, f.code()); f.reset();
    steps(3, 3800, -6000, 250); TEST_ASSERT_EQUAL(FB_FAULT_OVERCURRENT, f.code()); f.reset();
    steps(3, 3800, 0, 250, false); TEST_ASSERT_EQUAL(FB_FAULT_SENSOR, f.code());
}
static void test_extreme_signed_current_faults() {
    steps(3, 3800, INT32_MIN, 250); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state()); TEST_ASSERT_EQUAL(FB_FAULT_OVERCURRENT, f.code());
    f.reset();
    steps(3, 3800, INT32_MAX, 250); TEST_ASSERT_EQUAL(FB_FAULT_OVERCURRENT, f.code());
}
static void test_counters_saturate_instead_of_wrapping() {
    BatteryFaultFsm::Config c; c.safe_after = 255;
    f = BatteryFaultFsm{c};
    steps(3, 3800, 0, 650); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state());
    steps(254, 3800, 0, 650); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state());
    steps(1, 3800, 0, 650); TEST_ASSERT_EQUAL(FB_ST_SAFE, f.state());
}
static void test_safe_is_terminal_until_reset() {
    steps(13, 3800, 0, 650); TEST_ASSERT_EQUAL(FB_ST_SAFE, f.state());
    steps(50, 3800, 0, 250); TEST_ASSERT_EQUAL(FB_ST_SAFE, f.state());
    f.reset(); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state());
}

// ---- limp-home
static void test_body_normal_follows_commands() {
    LimpHomeFsm b{1000}; b.command(FB_LIGHT_HEAD | FB_LIGHT_TURN, 45);
    const BodyOutputs o = b.step(0, true, FB_ST_NORMAL);
    TEST_ASSERT_FALSE(o.limp_home); TEST_ASSERT_EQUAL(45, o.servo_deg); TEST_ASSERT_EQUAL(FB_LIGHT_HEAD | FB_LIGHT_TURN, o.lights);
}
static void test_body_limp_on_battery_fault() {
    LimpHomeFsm b{1000};
    const BodyOutputs o = b.step(0, true, FB_ST_FAULT);
    TEST_ASSERT_TRUE(o.limp_home); TEST_ASSERT_EQUAL(90, o.servo_deg); TEST_ASSERT_EQUAL(FB_LIGHT_FAULT, o.lights);
    TEST_ASSERT_TRUE(LimpHomeFsm{1000}.step(0, true, FB_ST_SAFE).limp_home);
}
static void test_body_limp_on_silence() { LimpHomeFsm b{1000}; TEST_ASSERT_TRUE(b.step(0, false, FB_ST_NORMAL).limp_home); }
static void test_body_recovers_after_hold() {
    LimpHomeFsm b{1000}; b.step(0, false, FB_ST_NORMAL);
    TEST_ASSERT_TRUE(b.step(100, true, FB_ST_NORMAL).limp_home);
    TEST_ASSERT_TRUE(b.step(1099, true, FB_ST_NORMAL).limp_home);
    TEST_ASSERT_FALSE(b.step(1100, true, FB_ST_NORMAL).limp_home);
}
static void test_body_relapse_resets_hold() {
    LimpHomeFsm b{1000}; b.step(0, false, FB_ST_NORMAL);
    b.step(100, true, FB_ST_NORMAL); b.step(900, false, FB_ST_NORMAL);
    b.step(1200, true, FB_ST_NORMAL);
    TEST_ASSERT_TRUE(b.step(2000, true, FB_ST_NORMAL).limp_home);
    TEST_ASSERT_FALSE(b.step(2200, true, FB_ST_NORMAL).limp_home);
}
static void test_body_recovery_across_tick_wrap() {
    LimpHomeFsm b{1000};
    b.step(UINT32_MAX - 20, false, FB_ST_NORMAL);
    b.step(UINT32_MAX - 10, true, FB_ST_NORMAL);
    TEST_ASSERT_TRUE(b.step(988, true, FB_ST_NORMAL).limp_home);
    TEST_ASSERT_FALSE(b.step(989, true, FB_ST_NORMAL).limp_home);
}
static void test_body_warning_is_not_limp() { LimpHomeFsm b{1000}; TEST_ASSERT_FALSE(b.step(0, true, FB_ST_WARNING).limp_home); }

// ---- heartbeat monitor
static void test_hb_never_seen_is_dead() { HeartbeatMonitor m{100}; TEST_ASSERT_FALSE(m.alive(0)); }
static void test_hb_timeout() {
    HeartbeatMonitor m{100}; m.feed(1000);
    TEST_ASSERT_TRUE(m.alive(1100)); TEST_ASSERT_FALSE(m.alive(1101));
}
static void test_hb_tick_wraparound() {
    HeartbeatMonitor m{100}; m.feed(0xFFFFFFF0u);
    TEST_ASSERT_TRUE(m.alive(20)); TEST_ASSERT_FALSE(m.alive(200));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_normal_stays_normal); RUN_TEST(test_warning_needs_debounce); RUN_TEST(test_single_glitch_ignored);
    RUN_TEST(test_warning_clears_with_hysteresis); RUN_TEST(test_overtemp_fault_then_safe); RUN_TEST(test_fault_latches_when_cleared);
    RUN_TEST(test_undervolt_and_overcurrent_and_sensor); RUN_TEST(test_extreme_signed_current_faults);
    RUN_TEST(test_counters_saturate_instead_of_wrapping); RUN_TEST(test_safe_is_terminal_until_reset);
    RUN_TEST(test_body_normal_follows_commands); RUN_TEST(test_body_limp_on_battery_fault); RUN_TEST(test_body_limp_on_silence);
    RUN_TEST(test_body_recovers_after_hold); RUN_TEST(test_body_relapse_resets_hold); RUN_TEST(test_body_recovery_across_tick_wrap);
    RUN_TEST(test_body_warning_is_not_limp);
    RUN_TEST(test_hb_never_seen_is_dead); RUN_TEST(test_hb_timeout); RUN_TEST(test_hb_tick_wraparound);
    return UNITY_END();
}
