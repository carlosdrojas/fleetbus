#include <string.h>
#include "unity.h"
#include "obd_poller.h"
#include "obd_allowlist.h"
#include "uart_link.h"
#include "can_msgs.h"

static fb_obd_t o;
void setUp(void) { fb_obd_init(&o, 0); }
void tearDown(void) {}

static void reply(uint32_t now, uint8_t pid, uint8_t a, uint8_t b, uint8_t len) {
    uint8_t d[8] = {len, 0x41, pid, a, b, 0x55, 0x55, 0x55};
    fb_obd_on_frame(&o, now, 0x7E8, d, 8);
}

static void test_poller_only_ever_emits_allowlisted_requests_in_round_robin(void) {
    fb_can_frame_t f; uint8_t seen[4] = {0}; int sent = 0;
    for (uint32_t t = 0; t < 10000; t += 10) {
        if (fb_obd_poll_tick(&o, t, &f)) {
            sent++;
            TEST_ASSERT_TRUE(fb_obd_frame_allowed(f.id, f.data, f.dlc));
            TEST_ASSERT_EQUAL(0x01, f.data[1]);
            if (f.data[2] == 0x0D) seen[0]++; if (f.data[2] == 0x0C) seen[1]++; if (f.data[2] == 0x05) seen[2]++; if (f.data[2] == 0x42) seen[3]++;
        }
    }
    TEST_ASSERT_EQUAL(99, sent);                               /* first one due at 100 ms, then one per 100 ms */
    for (int i = 0; i < 4; i++) { TEST_ASSERT_GREATER_OR_EQUAL(24, seen[i]); TEST_ASSERT_LESS_OR_EQUAL(25, seen[i]); }
}
static void test_poll_rate_is_bounded(void) {
    fb_can_frame_t f; int n = 0;
    for (uint32_t t = 0; t < 1000; t++) n += fb_obd_poll_tick(&o, t, &f);   /* ticked every ms: still <= 10 req/s */
    TEST_ASSERT_LESS_OR_EQUAL(10, n);
}
static void test_parses_standard_replies(void) {
    reply(100, 0x0D, 88, 0, 3); reply(110, 0x0C, 0x2B, 0x00, 4); reply(120, 0x05, 130, 0, 3); reply(130, 0x42, 0x37, 0xDC, 4);
    TEST_ASSERT_EQUAL(88, o.speed_kph);
    TEST_ASSERT_EQUAL(11008, o.rpm_quarter);          /* 0x2B00 quarter-rpm = 2752 rpm */
    TEST_ASSERT_EQUAL(130, o.coolant_raw);            /* 90 C */
    TEST_ASSERT_EQUAL(14300, o.ctrl_mv);              /* 0x37DC = 14300 mV */
    TEST_ASSERT_EQUAL(FB_DRIVE_DRIVING, o.drive);
}
static void test_ignores_non_replies_and_garbage(void) {
    uint8_t d[8] = {3, 0x41, 0x0D, 99, 0, 0, 0, 0};
    fb_obd_on_frame(&o, 50, 0x123, d, 8);                         /* not an ECU reply id */
    fb_obd_on_frame(&o, 50, 0x7DF, d, 8);                         /* our own request id is not a reply */
    uint8_t neg[8] = {3, 0x7F, 0x01, 0x11, 0, 0, 0, 0};            /* negative response */
    fb_obd_on_frame(&o, 50, 0x7E8, neg, 8);
    uint8_t shortf[2] = {3, 0x41};
    fb_obd_on_frame(&o, 50, 0x7E8, shortf, 2);
    TEST_ASSERT_FALSE(o.have_speed); TEST_ASSERT_EQUAL(1, o.bad_replies);
}
static void test_drive_state_transitions_and_sleep(void) {
    reply(100, 0x0D, 0, 0, 3); reply(110, 0x0C, 0x0D, 0x00, 4);          /* idling: rpm > 0, speed 0 */
    TEST_ASSERT_EQUAL(FB_DRIVE_IDLE, o.drive);
    reply(200, 0x0D, 30, 0, 3); TEST_ASSERT_EQUAL(FB_DRIVE_DRIVING, o.drive);
    reply(300, 0x0D, 0, 0, 3); reply(310, 0x0C, 0, 0, 4); reply(320, 0x42, 0x30, 0xD4, 4);   /* 12.5 V, engine off */
    TEST_ASSERT_EQUAL(FB_DRIVE_PARKED, o.drive);
    TEST_ASSERT_FALSE(fb_obd_should_sleep(&o, 20000));                    /* parked, but not long enough */
    TEST_ASSERT_TRUE(fb_obd_should_sleep(&o, 320 + FB_OBD_SLEEP_SILENCE_MS + 1));
}
static void test_silent_bus_means_parked_then_sleep(void) {
    TEST_ASSERT_FALSE(fb_obd_should_sleep(&o, 10000));
    TEST_ASSERT_TRUE(fb_obd_should_sleep(&o, FB_OBD_SLEEP_SILENCE_MS + 1));
}
static void test_engine_running_by_voltage_alone_is_idle(void) {
    reply(100, 0x42, 0x38, 0xA4, 4);                                      /* 14.5 V */
    TEST_ASSERT_EQUAL(FB_DRIVE_IDLE, o.drive);
}
static void test_vehicle_frame_matches_dbc(void) {
    reply(100, 0x0D, 88, 0, 3); reply(110, 0x0C, 0x2B, 0x00, 4);
    uint32_t id; uint8_t d[8]; TEST_ASSERT_EQUAL(8, fb_obd_vehicle_frame(&o, &id, d));
    struct can_msgs_vehicle_obd_t m; can_msgs_vehicle_obd_unpack(&m, d, 8);
    TEST_ASSERT_EQUAL(0x500, id); TEST_ASSERT_EQUAL(88, m.speed_kph); TEST_ASSERT_EQUAL(11008, m.rpm_quarter); TEST_ASSERT_EQUAL(1, m.proto_ver);
}

static void test_link_roundtrip_with_escaping(void) {
    uint8_t enc[FB_LINK_MAX_ENCODED]; fb_link_rx_t rx; fb_link_rx_init(&rx); fb_can_frame_t out;
    uint8_t data[8] = {0x7E, 0x7D, 0x00, 0xFF, 0x7E, 0x20, 0x5E, 0x5D};   /* every byte that needs care */
    size_t n = fb_link_encode(0x7DF, data, 8, enc);
    int got = 0;
    for (size_t i = 0; i < n; i++) got += fb_link_feed(&rx, enc[i], &out);
    got += fb_link_feed(&rx, FB_LINK_SOF, &out);                          /* next SOF closes the frame */
    TEST_ASSERT_EQUAL(1, got); TEST_ASSERT_EQUAL(0x7DF, out.id); TEST_ASSERT_EQUAL(8, out.dlc); TEST_ASSERT_EQUAL_MEMORY(data, out.data, 8);
}
static void test_link_rejects_corruption_and_resyncs(void) {
    uint8_t good[FB_LINK_MAX_ENCODED], bad[FB_LINK_MAX_ENCODED]; uint8_t d[3] = {1, 2, 3};
    size_t ng = fb_link_encode(0x100, d, 3, good);
    memcpy(bad, good, ng); bad[ng - 1] ^= 0x01;                          /* flip a CRC bit */
    fb_link_rx_t rx; fb_link_rx_init(&rx); fb_can_frame_t out; int got = 0;
    for (size_t i = 0; i < ng; i++) got += fb_link_feed(&rx, bad[i], &out);
    for (size_t i = 0; i < ng; i++) got += fb_link_feed(&rx, good[i], &out);   /* the SOF of this frame flushes the bad one */
    got += fb_link_feed(&rx, FB_LINK_SOF, &out);
    TEST_ASSERT_EQUAL(1, got); TEST_ASSERT_EQUAL(1, rx.crc_errors); TEST_ASSERT_EQUAL(0x100, out.id);
}
static void test_link_survives_random_noise_without_overflow(void) {
    fb_link_rx_t rx; fb_link_rx_init(&rx); fb_can_frame_t out; uint32_t s = 12345; int accepted = 0;
    for (int i = 0; i < 200000; i++) { s = s * 1664525u + 1013904223u; accepted += fb_link_feed(&rx, (uint8_t)(s >> 24), &out); }
    TEST_ASSERT_LESS_THAN(5, accepted);                                   /* a random stream essentially never forms a valid frame */
}
static void test_crc8_known_value(void) { TEST_ASSERT_EQUAL_HEX8(0xF4, fb_crc8((const uint8_t *)"123456789", 9)); }   /* CRC-8/SMBUS check */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_poller_only_ever_emits_allowlisted_requests_in_round_robin); RUN_TEST(test_poll_rate_is_bounded);
    RUN_TEST(test_parses_standard_replies); RUN_TEST(test_ignores_non_replies_and_garbage);
    RUN_TEST(test_drive_state_transitions_and_sleep); RUN_TEST(test_silent_bus_means_parked_then_sleep);
    RUN_TEST(test_engine_running_by_voltage_alone_is_idle); RUN_TEST(test_vehicle_frame_matches_dbc);
    RUN_TEST(test_link_roundtrip_with_escaping); RUN_TEST(test_link_rejects_corruption_and_resyncs);
    RUN_TEST(test_link_survives_random_noise_without_overflow); RUN_TEST(test_crc8_known_value);
    return UNITY_END();
}
