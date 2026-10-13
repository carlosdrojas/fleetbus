#include <string.h>
#include "unity.h"
#include "image.h"
#include "image_builder.h"
#include "fake_flash.h"
#include "fb_types.h"
#include "boot_logic.h"
#include "boot_meta.h"

static fake_flash_t ff; static fb_flash_t fl;
static uint8_t sk[64], pk[32], blob[8192], body[3000];
static size_t blob_len;
void setUp(void) {
    fake_flash_init(&ff, &fl); test_keypair(sk, pk);
    for (unsigned i = 0; i < sizeof body; i++) body[i] = (uint8_t)(i * 31 + 7);
    blob_len = test_build_image(blob, sizeof blob, sk, 5, 0, FB_NODE_BATTERY, FB_BOARD_HW_ID, body, sizeof body);
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
    blob_len = test_build_image(blob, sizeof blob, sk, 5, 1, FB_NODE_BATTERY, FB_BOARD_HW_ID, body, sizeof body);
    memcpy(slot0(), blob, blob_len); TEST_ASSERT_EQUAL(FB_IMG_ERR_SLOT, verify(0));
}
static void test_image_for_other_node_rejected(void) { uint32_t v; TEST_ASSERT_EQUAL(FB_IMG_ERR_NODE, fb_image_verify(&fl, 0, FB_NODE_BODY, 0, pk, &v)); }
static void test_downgrade_rejected(void) { TEST_ASSERT_EQUAL(FB_IMG_ERR_DOWNGRADE, verify(6)); TEST_ASSERT_EQUAL(FB_IMG_OK, verify(5)); }
static void test_oversize_rejected(void) {
    uint32_t big = FB_SLOT_SIZE; memcpy(slot0() + 12, &big, 4);   /* header says it's larger than the slot */
    TEST_ASSERT_EQUAL(FB_IMG_ERR_SIZE, verify(0));
}
static void test_image_for_other_board_rejected(void) {
    uint16_t other = FB_BOARD_HW_ID == FB_IMG_HW_G431 ? FB_IMG_HW_MSPM0G3507 : FB_IMG_HW_G431;
    blob_len = test_build_image(blob, sizeof blob, sk, 5, 0, FB_NODE_BATTERY, other, body, sizeof body);
    memcpy(slot0(), blob, blob_len); TEST_ASSERT_EQUAL(FB_IMG_ERR_HW, verify(0));
}

/* A normal boot verifies the chosen image once: P0 measured ~0.43 s per verify on the M0+ and the old code did two. */
static int hdr_reads;
static int counting_read(void *c, uint32_t a, uint8_t *o, size_t n) { if (a == FB_SLOT_A_ADDR) hdr_reads++; return fl.read(c, a, o, n); }
static void test_boot_verifies_chosen_image_once(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[0] = FB_SLOT_CONFIRMED; m.active = 0;
    TEST_ASSERT_EQUAL(0, fb_meta_store(&fl, &m));
    fb_flash_t counted = fl; counted.read = counting_read; hdr_reads = 0;
    fb_boot_result_t r;
    TEST_ASSERT_EQUAL(0, fb_boot_run(&counted, FB_NODE_BATTERY, pk, &r));
    TEST_ASSERT_EQUAL(0, r.slot); TEST_ASSERT_EQUAL_UINT32(5, r.version);
    TEST_ASSERT_EQUAL(1, hdr_reads);
}

/* Fresh board: metadata and slots are erased (on the MSPM0 that means garbage, not 0xFF). Nothing boots, nothing
 * crashes, and the bootloader can still persist metadata afterwards. */
static void test_fresh_board_boots_nothing(void) {
    fake_flash_init(&ff, &fl);
    fb_boot_result_t r;
    TEST_ASSERT_EQUAL(-1, fb_boot_run(&fl, FB_NODE_BATTERY, pk, &r));
    fb_boot_meta_t m; TEST_ASSERT_EQUAL(0, fb_meta_load(&fl, &m));
    TEST_ASSERT_EQUAL(FB_SLOT_EMPTY, m.status[0]); TEST_ASSERT_EQUAL(FB_SLOT_EMPTY, m.status[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_image); RUN_TEST(test_tampered_body_rejected); RUN_TEST(test_tampered_version_rejected);
    RUN_TEST(test_tampered_hash_rejected); RUN_TEST(test_wrong_key_rejected); RUN_TEST(test_unsigned_rejected);
    RUN_TEST(test_blank_slot_rejected); RUN_TEST(test_wrong_hw_rejected); RUN_TEST(test_image_for_other_slot_rejected);
    RUN_TEST(test_image_for_other_node_rejected); RUN_TEST(test_downgrade_rejected); RUN_TEST(test_oversize_rejected);
    RUN_TEST(test_image_for_other_board_rejected); RUN_TEST(test_boot_verifies_chosen_image_once); RUN_TEST(test_fresh_board_boots_nothing);
    return UNITY_END();
}
