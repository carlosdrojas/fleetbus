/* System-level scenarios on the host simulator: real node_core + boot logic + fake flash, two nodes on a bus. */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "fbsim.h"
#include "image.h"
#include "image_builder.h"
#include "fb_types.h"
#include "can_msgs.h"
#include "node_core.h"

static sim_node_t *bat, *body;
static uint8_t sk[64], pk[32];
static uint32_t now;
static int32_t mv, ma, dc;
static int bat_unplugged;
static struct { uint32_t id; uint8_t d[8]; uint8_t dlc; } gw_rx[256]; static int gw_n;

static size_t make_image(uint8_t *out, size_t cap, uint32_t ver, uint8_t slot, uint8_t node, uint8_t behavior) {
    static uint8_t body_[4000];
    memset(body_, 0xA0, sizeof body_); body_[0] = behavior; body_[1] = (uint8_t)ver;
    return test_build_image(out, cap, sk, ver, slot, node, FB_BOARD_HW_ID, body_, sizeof body_);
}

static void boot_fleet(void) {
    uint8_t img[8192];
    bat = sim_node_new(FB_NODE_BATTERY, pk); body = sim_node_new(FB_NODE_BODY, pk);
    size_t n = make_image(img, sizeof img, 1, 0, FB_NODE_BATTERY, 0); sim_node_provision(bat, 0, img, (uint32_t)n);
    n = make_image(img, sizeof img, 1, 0, FB_NODE_BODY, 0); sim_node_provision(body, 0, img, (uint32_t)n);
    now = 1000; sim_node_power_on(bat, now); sim_node_power_on(body, now);
    mv = 3800; ma = 500; dc = 250; bat_unplugged = 0; gw_n = 0;
}
void setUp(void) { uint8_t s[64]; test_keypair(s, pk); memcpy(sk, s, 64); boot_fleet(); }
void tearDown(void) { sim_node_free(bat); sim_node_free(body); }

static void deliver(sim_node_t *from, sim_node_t *other) {
    uint32_t id; uint8_t d[8], dlc;
    while (sim_node_pop_tx(from, &id, d, &dlc)) {
        if (other && !(from == bat && bat_unplugged)) sim_node_rx(other, now, id, d, dlc);
        if (id == CAN_MSGS_OTA_ACK_FRAME_ID && gw_n < 256) { gw_rx[gw_n].id = id; memcpy(gw_rx[gw_n].d, d, 8); gw_rx[gw_n].dlc = dlc; gw_n++; }
    }
}
static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        now += 10;
        if (!bat_unplugged) sim_node_tick(bat, now, mv, ma, dc, 1);
        sim_node_tick(body, now, 3800, 0, 250, 1);
        deliver(bat, body); deliver(body, bat);
    }
}

/* ---- gateway-side OTA driver (what gateway/ does over real CAN) ---- */
static void send_ctrl(sim_node_t *n, int node, int cmd, int total, int slot) {
    struct can_msgs_ota_ctrl_t c = {.cmd = (uint8_t)cmd, .total_blocks = (uint16_t)total, .slot = (uint8_t)slot, .target_node = (uint8_t)node, .proto_ver = 1};
    uint8_t b[8]; can_msgs_ota_ctrl_pack(b, &c, 8); sim_node_rx(n, now, CAN_MSGS_OTA_CTRL_FRAME_ID, b, 8);
}
static int last_ack_status(void) { struct can_msgs_ota_ack_t a; if (!gw_n) return -1; can_msgs_ota_ack_unpack(&a, gw_rx[gw_n - 1].d, 8); return a.status; }
static int ota_push(sim_node_t *n, int node, int slot, const uint8_t *blob, size_t len) {
    int total = (int)((len + 5) / 6);
    gw_n = 0; send_ctrl(n, node, 1, total, slot); deliver(n, 0);
    if (last_ack_status() != 0) return -1;
    for (int b = 0; b < total; b++) {
        struct can_msgs_ota_data_t d = {.block = (uint16_t)b};
        uint8_t t[6]; memset(t, 0xFF, 6); size_t off = (size_t)b * 6; memcpy(t, blob + off, len - off < 6 ? len - off : 6);
        d.d0 = t[0]; d.d1 = t[1]; d.d2 = t[2]; d.d3 = t[3]; d.d4 = t[4]; d.d5 = t[5];
        uint8_t raw[8]; can_msgs_ota_data_pack(raw, &d, 8); sim_node_rx(n, now, CAN_MSGS_OTA_DATA_FRAME_ID, raw, 8);
        if (b % 8 == 7) deliver(n, 0);
    }
    deliver(n, 0);
    gw_n = 0; send_ctrl(n, node, 3, total, slot); deliver(n, 0);
    return last_ack_status();      /* 3 = DONE, 2 = VERIFY_FAILED */
}

static void test_fleet_boots_and_exchanges_heartbeats(void) {
    run(2000);
    TEST_ASSERT_EQUAL(FB_ST_NORMAL, sim_node_state(bat)); TEST_ASSERT_EQUAL(FB_ST_NORMAL, sim_node_state(body));
    TEST_ASSERT_EQUAL(0, sim_node_limp(body));
}
/* Lite option 4: the body node keeps the battery's latest measurements for its gauge display. */
static void test_body_node_holds_latest_battery_status(void) {
    int32_t b[5];
    TEST_ASSERT_EQUAL(0, sim_node_peer_battery(body, now, b));
    mv = 3712; ma = -842; dc = 251;
    run(300);
    TEST_ASSERT_EQUAL(1, sim_node_peer_battery(body, now, b));
    TEST_ASSERT_EQUAL_INT32(3712, b[0]); TEST_ASSERT_EQUAL_INT32(-842, b[1]); TEST_ASSERT_EQUAL_INT32(251, b[2]);
    TEST_ASSERT_EQUAL_INT32((3712 - 3000) * 100 / 1200, b[3]);
    TEST_ASSERT_TRUE(b[4] >= 0 && b[4] <= 110);            /* refreshed every 100 ms */
    bat_unplugged = 1; run(1000);
    sim_node_peer_battery(body, now, b);
    TEST_ASSERT_TRUE(b[4] >= 1000);                        /* stale value ages: the display can show "battery silent" */
}
static void test_overtemp_puts_body_in_limp_home(void) {
    run(2500); uint32_t t0 = now; dc = 700;
    while (!sim_node_limp(body) && now - t0 < 3000) run(10);
    TEST_ASSERT_EQUAL(1, sim_node_limp(body));
    printf("    [measured in sim] overtemp -> body limp-home after %u ms\n", (unsigned)(now - t0));
    TEST_ASSERT_LESS_THAN(600, now - t0);
    TEST_ASSERT_EQUAL(FB_LIGHT_FAULT, sim_node_lights(body)); TEST_ASSERT_EQUAL(90, sim_node_servo(body));
}
static void test_unplugged_battery_puts_body_in_limp_home(void) {
    run(2500); TEST_ASSERT_EQUAL(0, sim_node_limp(body));
    bat_unplugged = 1; uint32_t t0 = now;
    while (!sim_node_limp(body) && now - t0 < 3000) run(10);
    TEST_ASSERT_EQUAL(1, sim_node_limp(body));
    printf("    [measured in sim] battery unplugged -> body limp-home after %u ms\n", (unsigned)(now - t0));
    TEST_ASSERT_LESS_OR_EQUAL(FB_PEER_TIMEOUT_MS + 20, now - t0);
    bat_unplugged = 0; run(2500); TEST_ASSERT_EQUAL(0, sim_node_limp(body));   /* recovers once the battery is back */
}
static void test_good_ota_updates_confirms_and_raises_floor(void) {
    uint8_t img[8192]; run(2500);
    size_t n = make_image(img, sizeof img, 2, 1, FB_NODE_BODY, 0);
    TEST_ASSERT_EQUAL(3, ota_push(body, FB_NODE_BODY, 1, img, n));
    for (int i = 0; i < 40 && sim_node_slot(body) != 1; i++) run(10);   /* the app waits 100 ms before resetting */
    TEST_ASSERT_EQUAL(1, sim_node_slot(body)); TEST_ASSERT_EQUAL_UINT32(2, sim_node_image_version(body));
    TEST_ASSERT_EQUAL(FB_SLOT_TRIAL, sim_node_slot_status(body, 1));
    run(2000);
    TEST_ASSERT_EQUAL(FB_SLOT_CONFIRMED, sim_node_slot_status(body, 1)); TEST_ASSERT_EQUAL_UINT32(2, sim_node_min_version(body));
    TEST_ASSERT_EQUAL(0, sim_node_limp(body));
}
static void test_hanging_ota_image_rolls_back(void) {
    uint8_t img[8192]; run(2500);
    size_t n = make_image(img, sizeof img, 2, 1, FB_NODE_BODY, 1 /* hangs */);
    TEST_ASSERT_EQUAL(3, ota_push(body, FB_NODE_BODY, 1, img, n));
    run(20000);
    TEST_ASSERT_EQUAL(0, sim_node_slot(body)); TEST_ASSERT_EQUAL_UINT32(1, sim_node_image_version(body));
    TEST_ASSERT_EQUAL(FB_SLOT_BAD, sim_node_slot_status(body, 1)); TEST_ASSERT_EQUAL(FB_SLOT_CONFIRMED, sim_node_slot_status(body, 0));
    TEST_ASSERT_TRUE(sim_node_alive(body)); TEST_ASSERT_EQUAL_UINT32(FB_BOOT_MAX_ATTEMPTS, sim_node_resets_by_wdg(body));
    TEST_ASSERT_EQUAL_UINT32(1, sim_node_min_version(body));
}
static void test_unhealthy_ota_image_misses_deadline_and_rolls_back(void) {
    uint8_t img[8192]; run(2500);
    size_t n = make_image(img, sizeof img, 2, 1, FB_NODE_BODY, 2 /* never healthy */);
    TEST_ASSERT_EQUAL(3, ota_push(body, FB_NODE_BODY, 1, img, n));
    run(60000);
    TEST_ASSERT_EQUAL(0, sim_node_slot(body)); TEST_ASSERT_EQUAL(FB_SLOT_BAD, sim_node_slot_status(body, 1));
    TEST_ASSERT_EQUAL(0, sim_node_limp(body));
}
static void test_downgrade_and_wrong_node_images_rejected(void) {
    uint8_t img[8192]; run(500);
    size_t n = make_image(img, sizeof img, 1, 1, FB_NODE_BODY, 0);       /* same version as running: not newer */
    TEST_ASSERT_EQUAL(2, ota_push(body, FB_NODE_BODY, 1, img, n));
    n = make_image(img, sizeof img, 5, 1, FB_NODE_BATTERY, 0);           /* built for the other node */
    TEST_ASSERT_EQUAL(2, ota_push(body, FB_NODE_BODY, 1, img, n));
    TEST_ASSERT_EQUAL(0, sim_node_slot(body)); TEST_ASSERT_EQUAL(FB_SLOT_EMPTY, sim_node_slot_status(body, 1));
}
static void test_corrupted_transfer_rejected(void) {
    uint8_t img[8192]; run(500);
    size_t n = make_image(img, sizeof img, 3, 1, FB_NODE_BODY, 0); img[2000] ^= 0x10;
    TEST_ASSERT_EQUAL(2, ota_push(body, FB_NODE_BODY, 1, img, n));
    TEST_ASSERT_EQUAL(FB_SLOT_EMPTY, sim_node_slot_status(body, 1)); TEST_ASSERT_EQUAL_UINT32(1, sim_node_boots(body));
}
static void test_begin_into_running_slot_refused(void) {
    uint8_t img[8192]; run(500); size_t n = make_image(img, sizeof img, 2, 0, FB_NODE_BODY, 0);
    TEST_ASSERT_EQUAL(-1, ota_push(body, FB_NODE_BODY, 0, img, n));
}
static void test_flash_corruption_of_active_slot_falls_back_to_unavailable_then_stays_in_bootloader(void) {
    run(500); sim_node_flip_byte(body, 0, 1000); sim_node_power_on(body, now);
    TEST_ASSERT_FALSE(sim_node_alive(body));            /* only slot is corrupt, nothing else to run: stay in bootloader */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fleet_boots_and_exchanges_heartbeats); RUN_TEST(test_body_node_holds_latest_battery_status); RUN_TEST(test_overtemp_puts_body_in_limp_home);
    RUN_TEST(test_unplugged_battery_puts_body_in_limp_home); RUN_TEST(test_good_ota_updates_confirms_and_raises_floor);
    RUN_TEST(test_hanging_ota_image_rolls_back); RUN_TEST(test_unhealthy_ota_image_misses_deadline_and_rolls_back);
    RUN_TEST(test_downgrade_and_wrong_node_images_rejected); RUN_TEST(test_corrupted_transfer_rejected);
    RUN_TEST(test_begin_into_running_slot_refused); RUN_TEST(test_flash_corruption_of_active_slot_falls_back_to_unavailable_then_stays_in_bootloader);
    return UNITY_END();
}
