#include <string.h>
#include "unity.h"
#include "obd_allowlist.h"
void setUp(void) {}
void tearDown(void) {}
static void test_allowed_requests_build(void) {
    uint8_t f[8];
    TEST_ASSERT_EQUAL(0, fb_obd_build_request(0x01, 0x0C, f)); TEST_ASSERT_TRUE(fb_obd_frame_allowed(0x7DF, f, 8));
    TEST_ASSERT_EQUAL(0, fb_obd_build_request(0x09, 0x02, f)); TEST_ASSERT_TRUE(fb_obd_frame_allowed(0x7DF, f, 8));
}
static void test_every_other_mode_rejected(void) {
    /* Exhaustive: of all 256 modes x 256 PIDs, only the allowlisted mode 01/09 pairs may ever be built. */
    int allowed = 0; uint8_t f[8];
    for (int m = 0; m < 256; m++) for (int p = 0; p < 256; p++) {
        uint8_t before[8]; memset(f, 0xEE, 8); memcpy(before, f, 8);
        if (fb_obd_build_request((uint8_t)m, (uint8_t)p, f) == 0) { allowed++; TEST_ASSERT_TRUE(m == 0x01 || m == 0x09); }
        else TEST_ASSERT_EQUAL_MEMORY(before, f, 8);
    }
    TEST_ASSERT_EQUAL(10, allowed);   /* 8 mode-01 PIDs + 2 mode-09 PIDs */
}
static void test_dangerous_services_blocked(void) {
    uint8_t f[8] = {0x02, 0, 0, 0x55, 0x55, 0x55, 0x55, 0x55};
    const uint8_t modes[] = {0x02, 0x03, 0x04, 0x07, 0x08, 0x0A, 0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x2E, 0x31, 0x34, 0x3E, 0x85};
    for (unsigned i = 0; i < sizeof modes; i++) { f[1] = modes[i]; TEST_ASSERT_FALSE(fb_obd_frame_allowed(0x7DF, f, 8)); }
}
static void test_wrong_id_or_shape_blocked(void) {
    uint8_t f[8]; fb_obd_build_request(0x01, 0x0D, f);
    TEST_ASSERT_FALSE(fb_obd_frame_allowed(0x7E0, f, 8)); TEST_ASSERT_FALSE(fb_obd_frame_allowed(0x7DF, f, 7));
    f[5] = 0x00; TEST_ASSERT_FALSE(fb_obd_frame_allowed(0x7DF, f, 8));
}
int main(void) { UNITY_BEGIN(); RUN_TEST(test_allowed_requests_build); RUN_TEST(test_every_other_mode_rejected); RUN_TEST(test_dangerous_services_blocked); RUN_TEST(test_wrong_id_or_shape_blocked); return UNITY_END(); }
