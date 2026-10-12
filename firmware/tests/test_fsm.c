#include "unity.h"
#include "fault_fsm.h"
#include "body_fsm.h"
#include "hb_monitor.h"
static fb_fault_fsm_t f;
void setUp(void) { fb_fault_cfg_t c; fb_fault_default_cfg(&c); fb_fault_init(&f, &c); }
void tearDown(void) {}
static void steps(int n, int mv, int ma, int dc, int ok) { for (int i = 0; i < n; i++) fb_fault_step(&f, mv, ma, dc, ok); }

static void test_normal_stays_normal(void) { steps(100, 3800, 500, 250, 1); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state); }
static void test_warning_needs_debounce(void) {
    steps(2, 3800, 0, 500, 1); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state);
    steps(1, 3800, 0, 500, 1); TEST_ASSERT_EQUAL(FB_ST_WARNING, f.state);
}
static void test_single_glitch_ignored(void) { steps(2, 3800, 0, 500, 1); steps(1, 3800, 0, 250, 1); steps(2, 3800, 0, 500, 1); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state); }
static void test_warning_clears_with_hysteresis(void) {
    steps(3, 3800, 0, 500, 1); TEST_ASSERT_EQUAL(FB_ST_WARNING, f.state);
    steps(5, 3800, 0, 440, 1); TEST_ASSERT_EQUAL(FB_ST_WARNING, f.state);   /* inside hysteresis band */
    steps(3, 3800, 0, 400, 1); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state);
}
static void test_overtemp_fault_then_safe(void) {
    steps(3, 3800, 0, 650, 1); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state); TEST_ASSERT_EQUAL(FB_FAULT_OVERTEMP, f.code);
    steps(10, 3800, 0, 650, 1); TEST_ASSERT_EQUAL(FB_ST_SAFE, f.state);
}
static void test_fault_latches_when_cleared(void) { steps(3, 2900, 0, 250, 1); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state); steps(5, 3800, 0, 250, 1); TEST_ASSERT_EQUAL(FB_ST_FAULT, f.state); fb_fault_reset(&f); TEST_ASSERT_EQUAL(FB_ST_NORMAL, f.state); }
static void test_undervolt_and_overcurrent_and_sensor(void) {
    steps(3, 2900, 0, 250, 1); TEST_ASSERT_EQUAL(FB_FAULT_UNDERVOLT, f.code); fb_fault_reset(&f);
    steps(3, 3800, -6000, 250, 1); TEST_ASSERT_EQUAL(FB_FAULT_OVERCURRENT, f.code); fb_fault_reset(&f);
    steps(3, 3800, 0, 250, 0); TEST_ASSERT_EQUAL(FB_FAULT_SENSOR, f.code);
}

static fb_body_fsm_t b; static fb_body_out_t o;
static void body_setup(void) { fb_body_init(&b, 1000); }
static void test_body_normal_follows_commands(void) { body_setup(); b.cmd_lights = FB_LIGHT_HEAD | FB_LIGHT_TURN; b.cmd_servo_deg = 45; fb_body_step(&b, 0, 1, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(0, o.limp_home); TEST_ASSERT_EQUAL(45, o.servo_deg); TEST_ASSERT_EQUAL(FB_LIGHT_HEAD | FB_LIGHT_TURN, o.lights); }
static void test_body_limp_on_battery_fault(void) { body_setup(); fb_body_step(&b, 0, 1, FB_ST_FAULT, &o); TEST_ASSERT_EQUAL(1, o.limp_home); TEST_ASSERT_EQUAL(90, o.servo_deg); TEST_ASSERT_EQUAL(FB_LIGHT_FAULT, o.lights); }
static void test_body_limp_on_silence(void) { body_setup(); fb_body_step(&b, 0, 0, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(1, o.limp_home); }
static void test_body_recovers_after_hold(void) {
    body_setup(); fb_body_step(&b, 0, 0, FB_ST_NORMAL, &o);
    fb_body_step(&b, 100, 1, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(1, o.limp_home);
    fb_body_step(&b, 1099, 1, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(1, o.limp_home);
    fb_body_step(&b, 1100, 1, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(0, o.limp_home);
}
static void test_body_relapse_resets_hold(void) {
    body_setup(); fb_body_step(&b, 0, 0, FB_ST_NORMAL, &o);
    fb_body_step(&b, 100, 1, FB_ST_NORMAL, &o); fb_body_step(&b, 900, 0, FB_ST_NORMAL, &o);
    fb_body_step(&b, 1200, 1, FB_ST_NORMAL, &o); fb_body_step(&b, 2000, 1, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(1, o.limp_home);
    fb_body_step(&b, 2200, 1, FB_ST_NORMAL, &o); TEST_ASSERT_EQUAL(0, o.limp_home);
}

static void test_hb_never_seen_is_dead(void) { fb_hb_monitor_t m; fb_hb_init(&m, 100); TEST_ASSERT_FALSE(fb_hb_peer_alive(&m, 0)); }
static void test_hb_timeout(void) { fb_hb_monitor_t m; fb_hb_init(&m, 100); fb_hb_feed(&m, 1000); TEST_ASSERT_TRUE(fb_hb_peer_alive(&m, 1100)); TEST_ASSERT_FALSE(fb_hb_peer_alive(&m, 1101)); }
static void test_hb_tick_wraparound(void) { fb_hb_monitor_t m; fb_hb_init(&m, 100); fb_hb_feed(&m, 0xFFFFFFF0u); TEST_ASSERT_TRUE(fb_hb_peer_alive(&m, 20)); TEST_ASSERT_FALSE(fb_hb_peer_alive(&m, 200)); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_stays_normal); RUN_TEST(test_warning_needs_debounce); RUN_TEST(test_single_glitch_ignored);
    RUN_TEST(test_warning_clears_with_hysteresis); RUN_TEST(test_overtemp_fault_then_safe); RUN_TEST(test_fault_latches_when_cleared);
    RUN_TEST(test_undervolt_and_overcurrent_and_sensor);
    RUN_TEST(test_body_normal_follows_commands); RUN_TEST(test_body_limp_on_battery_fault); RUN_TEST(test_body_limp_on_silence);
    RUN_TEST(test_body_recovers_after_hold); RUN_TEST(test_body_relapse_resets_hold);
    RUN_TEST(test_hb_never_seen_is_dead); RUN_TEST(test_hb_timeout); RUN_TEST(test_hb_tick_wraparound);
    return UNITY_END();
}
