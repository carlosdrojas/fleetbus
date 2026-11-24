// The wire format, byte by byte. If one of these fails, the two boards (or a logic analyser trace) disagree.
#include <cstring>
#include "unity.h"
#include "can_msgs.hpp"
#include "fb_types.h"
using namespace fleetbus::msg;
void setUp() {}
void tearDown() {}

static void test_heartbeat_layout_and_roundtrip() {
    Heartbeat a; a.node_id = 2; a.fw_version = 0x0102; a.uptime_s = 0x123456; a.state = FB_ST_LIMP_HOME; a.proto_ver = FB_PROTO_VER;
    uint8_t b[8];
    a.pack(b);
    const uint8_t want[8] = {0x02, 0x02, 0x01, 0x56, 0x34, 0x12, FB_ST_LIMP_HOME, FB_PROTO_VER};   // little-endian
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b, 8);
    Heartbeat r;
    TEST_ASSERT_TRUE(r.unpack(b, 8));
    TEST_ASSERT_EQUAL_UINT16(0x0102, r.fw_version); TEST_ASSERT_EQUAL_UINT32(0x123456, r.uptime_s);
    TEST_ASSERT_EQUAL_UINT8(FB_ST_LIMP_HOME, r.state);
}
static void test_battery_signed_fields_survive() {
    BatteryStatus a; a.pack_mv = 3700; a.ma = -1500; a.temp_dc = -50; a.soc_pct = 80; a.proto_ver = 1;
    uint8_t b[8];
    a.pack(b);
    TEST_ASSERT_EQUAL_UINT8(0x24, b[2]); TEST_ASSERT_EQUAL_UINT8(0xFA, b[3]);   // -1500 = 0xFA24
    BatteryStatus r;
    TEST_ASSERT_TRUE(r.unpack(b, 8));
    TEST_ASSERT_EQUAL_UINT16(3700, r.pack_mv); TEST_ASSERT_EQUAL_INT16(-1500, r.ma); TEST_ASSERT_EQUAL_INT16(-50, r.temp_dc);
}
static void test_body_status_is_four_bytes() {
    BodyStatus a; a.lights = FB_LIGHT_FAULT; a.servo_deg = 90; a.limp_home = 1; a.proto_ver = 1;
    uint8_t b[8] = {};
    a.pack(b);
    const uint8_t want[4] = {FB_LIGHT_FAULT, 90, 1, 1};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b, 4);
    TEST_ASSERT_EQUAL_UINT8(4, kBodyStatusDlc);
}
static void test_short_frames_are_rejected() {
    uint8_t b[8] = {};
    Heartbeat h; BatteryStatus s; BodyStatus bs; FaultEvent fe;
    TEST_ASSERT_FALSE(h.unpack(b, 7)); TEST_ASSERT_FALSE(s.unpack(b, 7));
    TEST_ASSERT_FALSE(bs.unpack(b, 3)); TEST_ASSERT_FALSE(fe.unpack(b, 7));
}
static void test_ids_order_heartbeats_first() {
    TEST_ASSERT_EQUAL_UINT32(0x100, kHeartbeatBase);
    TEST_ASSERT_TRUE(kHeartbeatBase + FB_NODE_BODY < kBatteryStatusId);
    TEST_ASSERT_TRUE(kBatteryStatusId < kBodyStatusId && kBodyStatusId < kFaultEventId);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_heartbeat_layout_and_roundtrip); RUN_TEST(test_battery_signed_fields_survive);
    RUN_TEST(test_body_status_is_four_bytes); RUN_TEST(test_short_frames_are_rejected); RUN_TEST(test_ids_order_heartbeats_first);
    return UNITY_END();
}
