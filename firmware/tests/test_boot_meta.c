#include <string.h>
#include "unity.h"
#include "boot_meta.h"
#include "fake_flash.h"

static fake_flash_t ff; static fb_flash_t fl;
void setUp(void) { fake_flash_init(&ff, &fl); }
void tearDown(void) {}

static int ok_all(void *c, int s) { (void)c; (void)s; return 1; }
static int ok_only(void *c, int s) { return *(int *)c == s; }

static void test_blank_flash_gives_default(void) { fb_boot_meta_t m; TEST_ASSERT_EQUAL(-1, fb_meta_load(&fl, &m)); TEST_ASSERT_EQUAL(FB_SLOT_EMPTY, m.status[0]); }
static void test_store_load_roundtrip_and_seq(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[0] = FB_SLOT_CONFIRMED; m.min_version = 5;
    TEST_ASSERT_EQUAL(0, fb_meta_store(&fl, &m));
    fb_boot_meta_t r; TEST_ASSERT_EQUAL(0, fb_meta_load(&fl, &r));
    TEST_ASSERT_EQUAL_UINT32(1, r.seq); TEST_ASSERT_EQUAL_UINT32(5, r.min_version);
    r.min_version = 6; fb_meta_store(&fl, &r); fb_meta_load(&fl, &r);
    TEST_ASSERT_EQUAL_UINT32(2, r.seq); TEST_ASSERT_EQUAL_UINT32(6, r.min_version);
}
static void test_power_loss_at_every_point_keeps_a_valid_copy(void) {
    for (int stage = 0; stage < 2; stage++) {          /* 0 = tear the erase, 1 = tear the write */
        fake_flash_init(&ff, &fl);
        fb_boot_meta_t m; fb_meta_default(&m); m.min_version = 1; fb_meta_store(&fl, &m);   /* seq 1 */
        m.min_version = 2; fb_meta_store(&fl, &m);                                          /* seq 2 */
        fake_flash_power_fail_in(&ff, stage);
        m.min_version = 3; (void)fb_meta_store(&fl, &m);                                    /* torn */
        fake_flash_power_on(&ff);
        fb_boot_meta_t r;
        TEST_ASSERT_EQUAL_MESSAGE(0, fb_meta_load(&fl, &r), "no valid metadata after power loss");
        TEST_ASSERT_EQUAL_UINT32(2, r.min_version);   /* the previous good copy survives */
        TEST_ASSERT_EQUAL(0, fb_meta_store(&fl, &r)); /* and the system can keep going */
        fb_meta_load(&fl, &r); TEST_ASSERT_EQUAL_UINT32(3, r.seq); TEST_ASSERT_EQUAL_UINT32(2, r.min_version);
    }
}
static void test_new_slot_becomes_trial_and_boots(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[0] = FB_SLOT_CONFIRMED; m.status[1] = FB_SLOT_NEW;
    m.active = 1;
    TEST_ASSERT_EQUAL(1, fb_boot_decide(&m, ok_all, NULL));
    TEST_ASSERT_EQUAL(FB_SLOT_TRIAL, m.status[1]); TEST_ASSERT_EQUAL(1, m.attempts);
}
static void test_unconfirmed_trial_rolls_back_after_max_attempts(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[0] = FB_SLOT_CONFIRMED; m.status[1] = FB_SLOT_NEW; m.active = 1;
    for (unsigned i = 0; i < FB_BOOT_MAX_ATTEMPTS; i++) TEST_ASSERT_EQUAL(1, fb_boot_decide(&m, ok_all, NULL));
    TEST_ASSERT_EQUAL(0, fb_boot_decide(&m, ok_all, NULL));      /* next boot: fall back */
    TEST_ASSERT_EQUAL(FB_SLOT_BAD, m.status[1]);
    TEST_ASSERT_EQUAL(0, fb_boot_decide(&m, ok_all, NULL));      /* stays on the good slot */
}
static void test_failed_verify_marks_bad_and_falls_back(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[0] = FB_SLOT_CONFIRMED; m.status[1] = FB_SLOT_NEW; m.active = 1;
    int only0 = 0;
    TEST_ASSERT_EQUAL(0, fb_boot_decide(&m, ok_only, &only0));
    TEST_ASSERT_EQUAL(FB_SLOT_BAD, m.status[1]);
}
static void test_nothing_bootable(void) {
    fb_boot_meta_t m; fb_meta_default(&m);
    TEST_ASSERT_EQUAL(-1, fb_boot_decide(&m, ok_all, NULL));
    m.status[0] = FB_SLOT_CONFIRMED; int only1 = 1;
    TEST_ASSERT_EQUAL(-1, fb_boot_decide(&m, ok_only, &only1));
}
static void test_confirm_raises_antidowngrade_floor(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[1] = FB_SLOT_NEW; m.version[1] = 7; m.active = 1; fb_meta_store(&fl, &m);
    fb_boot_decide(&m, ok_all, NULL); fb_meta_store(&fl, &m);
    TEST_ASSERT_EQUAL(0, fb_meta_confirm(&fl, 1));
    fb_boot_meta_t r; fb_meta_load(&fl, &r);
    TEST_ASSERT_EQUAL(FB_SLOT_CONFIRMED, r.status[1]); TEST_ASSERT_EQUAL_UINT32(7, r.min_version); TEST_ASSERT_EQUAL(0, r.attempts);
    TEST_ASSERT_EQUAL(0, fb_meta_confirm(&fl, 1));    /* idempotent */
}
static void test_confirm_of_non_trial_does_not_confirm_a_bad_slot(void) {
    fb_boot_meta_t m; fb_meta_default(&m); m.status[0] = FB_SLOT_BAD; fb_meta_store(&fl, &m);
    fb_meta_confirm(&fl, 0); fb_meta_load(&fl, &m); TEST_ASSERT_EQUAL(FB_SLOT_BAD, m.status[0]);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_blank_flash_gives_default); RUN_TEST(test_store_load_roundtrip_and_seq);
    RUN_TEST(test_power_loss_at_every_point_keeps_a_valid_copy); RUN_TEST(test_new_slot_becomes_trial_and_boots);
    RUN_TEST(test_unconfirmed_trial_rolls_back_after_max_attempts); RUN_TEST(test_failed_verify_marks_bad_and_falls_back);
    RUN_TEST(test_nothing_bootable); RUN_TEST(test_confirm_raises_antidowngrade_floor);
    RUN_TEST(test_confirm_of_non_trial_does_not_confirm_a_bad_slot);
    return UNITY_END();
}
