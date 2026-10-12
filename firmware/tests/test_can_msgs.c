#include <string.h>
#include "unity.h"
#include "can_msgs.h"
#include "fb_types.h"
void setUp(void) {}
void tearDown(void) {}
static void test_heartbeat_roundtrip(void) {
    struct can_msgs_heartbeat_t a = {.node_id = 2, .fw_version = 0x0102, .uptime_s = 70000, .state = FB_ST_NORMAL, .proto_ver = FB_PROTO_VER}, b;
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_INT(8, can_msgs_heartbeat_pack(buf, &a, sizeof buf));
    TEST_ASSERT_EQUAL_INT(0, can_msgs_heartbeat_unpack(&b, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(2, b.node_id); TEST_ASSERT_EQUAL_UINT16(0x0102, b.fw_version);
    TEST_ASSERT_EQUAL_UINT32(70000, b.uptime_s); TEST_ASSERT_EQUAL_UINT8(FB_PROTO_VER, b.proto_ver);
}
static void test_signed_battery(void) {
    struct can_msgs_battery_status_t a = {.pack_milli_volt = 3700, .current_milli_amp = -1500, .temp_deci_c = -50, .soc_pct = 80, .proto_ver = 1}, b;
    uint8_t buf[8];
    can_msgs_battery_status_pack(buf, &a, sizeof buf);
    can_msgs_battery_status_unpack(&b, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT16(-1500, b.current_milli_amp); TEST_ASSERT_EQUAL_INT16(-50, b.temp_deci_c);
}
static void test_ids(void) {
    TEST_ASSERT_EQUAL_UINT32(0x100, CAN_MSGS_HEARTBEAT_FRAME_ID);
    TEST_ASSERT_EQUAL_UINT32(0x200, CAN_MSGS_BATTERY_STATUS_FRAME_ID);
}
static void test_range_check(void) { TEST_ASSERT_FALSE(can_msgs_battery_status_soc_pct_is_in_range(101)); TEST_ASSERT_TRUE(can_msgs_battery_status_soc_pct_is_in_range(100)); }
int main(void) { UNITY_BEGIN(); RUN_TEST(test_heartbeat_roundtrip); RUN_TEST(test_signed_battery); RUN_TEST(test_ids); RUN_TEST(test_range_check); return UNITY_END(); }
