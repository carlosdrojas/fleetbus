#include <string.h>
#include "unity.h"
#include "ota_receiver.h"
#include "image.h"
#include "image_builder.h"
#include "fake_flash.h"
#include "fb_types.h"

static fake_flash_t ff; static fb_flash_t fl; static fb_ota_rx_t rx;
static uint8_t sk[64], pk[32], blob[8192], body[2003]; /* deliberately not a multiple of 6 or 8 */
static size_t blob_len; static uint16_t nblocks;
void setUp(void) {
    fake_flash_init(&ff, &fl); test_keypair(sk, pk); fb_ota_init(&rx, &fl);
    for (unsigned i = 0; i < sizeof body; i++) body[i] = (uint8_t)(i ^ 0x5A);
    blob_len = test_build_image(blob, sizeof blob, sk, 3, 1, FB_NODE_BATTERY, FB_IMG_HW_G431, body, sizeof body);
    nblocks = (uint16_t)((blob_len + 5) / 6);
}
void tearDown(void) {}
static int send(uint16_t b, uint16_t *next) {
    uint8_t d[6]; memset(d, 0xFF, 6);
    size_t off = (size_t)b * 6, n = blob_len > off ? (blob_len - off < 6 ? blob_len - off : 6) : 0;
    memcpy(d, blob + off, n);
    return fb_ota_block(&rx, b, d, next);
}
static int verify1(void) { uint32_t v; return fb_image_verify(&fl, 1, FB_NODE_BATTERY, 0, pk, &v); }

static void test_full_transfer_verifies(void) {
    uint16_t next;
    TEST_ASSERT_EQUAL(0, fb_ota_begin(&rx, 1, nblocks));
    for (uint16_t b = 0; b < nblocks; b++) TEST_ASSERT_EQUAL(FB_OTA_ACK_OK, send(b, &next));
    TEST_ASSERT_EQUAL(FB_OTA_DONE, rx.state); TEST_ASSERT_EQUAL(nblocks, next);
    TEST_ASSERT_EQUAL(FB_IMG_OK, verify1());
}
static void test_duplicates_are_reacked_not_rewritten(void) {
    uint16_t next; fb_ota_begin(&rx, 1, nblocks);
    send(0, &next); send(1, &next); int w = ff.write_count;
    TEST_ASSERT_EQUAL(FB_OTA_ACK_OK, send(0, &next)); TEST_ASSERT_EQUAL(2, next); TEST_ASSERT_EQUAL(w, ff.write_count);
}
static void test_gap_is_rejected_with_expected_block(void) {
    uint16_t next; fb_ota_begin(&rx, 1, nblocks); send(0, &next);
    TEST_ASSERT_EQUAL(FB_OTA_ACK_BAD_BLOCK, send(5, &next)); TEST_ASSERT_EQUAL(1, next);
    for (uint16_t b = 1; b < nblocks; b++) send(b, &next);     /* sender resumes from the expected block */
    TEST_ASSERT_EQUAL(FB_IMG_OK, verify1());
}
static void test_corrupted_block_fails_verify(void) {
    uint16_t next; fb_ota_begin(&rx, 1, nblocks);
    for (uint16_t b = 0; b < nblocks; b++) { if (b == 100) blob[b * 6 + 2] ^= 0x40; send(b, &next); }
    TEST_ASSERT_EQUAL(FB_OTA_DONE, rx.state); TEST_ASSERT_NOT_EQUAL(FB_IMG_OK, verify1());
}
static void test_power_loss_mid_transfer_never_yields_valid_image(void) {
    uint16_t next; fb_ota_begin(&rx, 1, nblocks);
    fake_flash_power_fail_in(&ff, 40);
    int acked_all = 1;
    for (uint16_t b = 0; b < nblocks; b++) if (send(b, &next) != FB_OTA_ACK_OK) { acked_all = 0; break; }
    TEST_ASSERT_FALSE(acked_all); TEST_ASSERT_EQUAL(FB_OTA_FAILED, rx.state);
    fake_flash_power_on(&ff); TEST_ASSERT_NOT_EQUAL(FB_IMG_OK, verify1());
}
static void test_blocks_before_begin_rejected(void) { uint16_t next; TEST_ASSERT_EQUAL(FB_OTA_ACK_BAD_BLOCK, send(0, &next)); }
static void test_begin_validates_size(void) { TEST_ASSERT_EQUAL(-1, fb_ota_begin(&rx, 1, 0)); TEST_ASSERT_EQUAL(-1, fb_ota_begin(&rx, 2, 10)); TEST_ASSERT_EQUAL(-1, fb_ota_begin(&rx, 1, (FB_SLOT_SIZE / 6) + 2)); }
static void test_only_target_slot_touched(void) {
    uint16_t next; memset(ff.mem + (FB_SLOT_A_ADDR - FB_FLASH_BASE), 0x11, 64);
    fb_ota_begin(&rx, 1, nblocks); for (uint16_t b = 0; b < nblocks; b++) send(b, &next);
    for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_HEX8(0x11, ff.mem[FB_SLOT_A_ADDR - FB_FLASH_BASE + i]);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_full_transfer_verifies); RUN_TEST(test_duplicates_are_reacked_not_rewritten); RUN_TEST(test_gap_is_rejected_with_expected_block);
    RUN_TEST(test_corrupted_block_fails_verify); RUN_TEST(test_power_loss_mid_transfer_never_yields_valid_image);
    RUN_TEST(test_blocks_before_begin_rejected); RUN_TEST(test_begin_validates_size); RUN_TEST(test_only_target_slot_touched);
    return UNITY_END();
}
