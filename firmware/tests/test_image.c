#include <string.h>
#include "unity.h"
#include "image.h"
#include "image_builder.h"
#include "fake_flash.h"
#include "fb_types.h"

static fake_flash_t ff; static fb_flash_t fl;
static uint8_t sk[64], pk[32], blob[8192], body[3000];
static size_t blob_len;
void setUp(void) {
    fake_flash_init(&ff, &fl); test_keypair(sk, pk);
    for (unsigned i = 0; i < sizeof body; i++) body[i] = (uint8_t)(i * 31 + 7);
    blob_len = test_build_image(blob, sizeof blob, sk, 5, 0, FB_NODE_BATTERY, FB_IMG_HW_G431, body, sizeof body);
    memcpy(ff.mem + (FB_SLOT_A_ADDR - FB_FLASH_BASE), blob, blob_len);
}
void tearDown(void) {}
static int verify(uint32_t min) { uint32_t v; return fb_image_verify(&fl, 0, FB_NODE_BATTERY, min, pk, &v); }
static uint8_t *slot0(void) { return ff.mem + (FB_SLOT_A_ADDR - FB_FLASH_BASE); }

static void test_valid_image(void) { uint32_t v = 0; TEST_ASSERT_EQUAL(FB_IMG_OK, fb_image_verify(&fl, 0, FB_NODE_BATTERY, 0, pk, &v)); TEST_ASSERT_EQUAL_UINT32(5, v); }
static void test_tampered_body_rejected(void) { slot0()[FB_IMG_HDR_SIZE + 1500] ^= 1; TEST_ASSERT_EQUAL(FB_IMG_ERR_HASH, verify(0)); }
static void test_tampered_version_rejected(void) { slot0()[8] = 99; TEST_ASSERT_EQUAL(FB_IMG_ERR_SIG, verify(0)); }
static void test_tampered_hash_rejected(void) { slot0()[FB_IMG_OFF_SHA] ^= 1; TEST_ASSERT_EQUAL(FB_IMG_ERR_SIG, verify(0)); }
static void test_wrong_key_rejected(void) { pk[0] ^= 1; TEST_ASSERT_EQUAL(FB_IMG_ERR_SIG, verify(0)); }
static void test_unsigned_rejected(void) { memset(slot0() + FB_IMG_OFF_SIG, 0, 64); TEST_ASSERT_EQUAL(FB_IMG_ERR_SIG, verify(0)); memset(slot0() + FB_IMG_OFF_SIG, 0xFF, 64); TEST_ASSERT_EQUAL(FB_IMG_ERR_SIG, verify(0)); }
static void test_blank_slot_rejected(void) { memset(slot0(), 0xFF, 4096); TEST_ASSERT_EQUAL(FB_IMG_ERR_MAGIC, verify(0)); }
static void test_wrong_hw_rejected(void) {
    blob_len = test_build_image(blob, sizeof blob, sk, 5, 0, FB_NODE_BATTERY, 0x9999, body, sizeof body);
    memcpy(slot0(), blob, blob_len); TEST_ASSERT_EQUAL(FB_IMG_ERR_HW, verify(0));
}
static void test_image_for_other_slot_rejected(void) {
    blob_len = test_build_image(blob, sizeof blob, sk, 5, 1, FB_NODE_BATTERY, FB_IMG_HW_G431, body, sizeof body);
    memcpy(slot0(), blob, blob_len); TEST_ASSERT_EQUAL(FB_IMG_ERR_SLOT, verify(0));
}
static void test_image_for_other_node_rejected(void) { uint32_t v; TEST_ASSERT_EQUAL(FB_IMG_ERR_NODE, fb_image_verify(&fl, 0, FB_NODE_BODY, 0, pk, &v)); }
static void test_downgrade_rejected(void) { TEST_ASSERT_EQUAL(FB_IMG_ERR_DOWNGRADE, verify(6)); TEST_ASSERT_EQUAL(FB_IMG_OK, verify(5)); }
static void test_oversize_rejected(void) {
    uint32_t big = FB_SLOT_SIZE; memcpy(slot0() + 12, &big, 4);   /* header says it's larger than the slot */
    TEST_ASSERT_EQUAL(FB_IMG_ERR_SIZE, verify(0));
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_image); RUN_TEST(test_tampered_body_rejected); RUN_TEST(test_tampered_version_rejected);
    RUN_TEST(test_tampered_hash_rejected); RUN_TEST(test_wrong_key_rejected); RUN_TEST(test_unsigned_rejected);
    RUN_TEST(test_blank_slot_rejected); RUN_TEST(test_wrong_hw_rejected); RUN_TEST(test_image_for_other_slot_rejected);
    RUN_TEST(test_image_for_other_node_rejected); RUN_TEST(test_downgrade_rejected); RUN_TEST(test_oversize_rejected);
    return UNITY_END();
}
