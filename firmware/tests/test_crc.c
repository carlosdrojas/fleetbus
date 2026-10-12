#include "unity.h"
#include "crc32.h"
void setUp(void) {}
void tearDown(void) {}
static void test_check_value(void) { TEST_ASSERT_EQUAL_HEX32(0xE3069283u, fb_crc32c((const uint8_t *)"123456789", 9)); }
static void test_empty(void) { TEST_ASSERT_EQUAL_HEX32(0u, fb_crc32c((const uint8_t *)"", 0)); }
static void test_chained(void) {
    const uint8_t *d = (const uint8_t *)"123456789";
    uint32_t c = fb_crc32c_update(0, d, 4);
    c = fb_crc32c_update(c, d + 4, 5);
    TEST_ASSERT_EQUAL_HEX32(0xE3069283u, c);
}
static void test_32_zero_bytes(void) { uint8_t z[32] = {0}; TEST_ASSERT_EQUAL_HEX32(0x8A9136AAu, fb_crc32c(z, 32)); } /* RFC 3720 B.4 */
int main(void) { UNITY_BEGIN(); RUN_TEST(test_check_value); RUN_TEST(test_empty); RUN_TEST(test_chained); RUN_TEST(test_32_zero_bytes); return UNITY_END(); }
