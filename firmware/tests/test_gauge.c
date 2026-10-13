/* Body-node instrument cluster (common/gauge_view.c) against an in-memory 160x128 RGB565 framebuffer. */
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "gauge_view.h"
#include "fbsim.h"
#include "image.h"
#include "image_builder.h"
#include "can_msgs.h"
#include "node_core.h"

static uint16_t fb[GV_H][GV_W], ref[GV_H][GV_W];
static int calls, oob;
static void fill_fb(void *ctx, int x, int y, int w, int h, uint16_t c) {
    uint16_t (*buf)[GV_W] = ctx;
    calls++;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > GV_W || y + h > GV_H) { oob++; return; }
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) buf[j][i] = c;
}
static gv_t g;
static gv_ops_t ops_for(void *buf) { return (gv_ops_t){ fill_fb, buf }; }
static gv_inputs_t normal(void) {
    return (gv_inputs_t){ .have = 1, .pack_mv = 3712, .ma = 842, .temp_dc = 251, .soc_pct = 59, .battery_alive = 1,
                          .battery_state = FB_ST_NORMAL, .limp_home = 0 };
}
static uint16_t banner_px(uint16_t (*buf)[GV_W]) { return buf[GV_H - 2][2]; }   /* banner corner, never text */

void setUp(void) { memset(fb, 0xAA, sizeof fb); calls = oob = 0; gv_init(&g, ops_for(fb)); }
void tearDown(void) { TEST_ASSERT_EQUAL_MESSAGE(0, oob, "drew outside the 160x128 screen"); }

static void test_first_draw_paints_title_and_normal_banner(void) {
    gv_inputs_t in = normal(); gv_draw(&g, &in);
    TEST_ASSERT_EQUAL_HEX16(GV_RGB565(31, 95, 168), fb[1][150]);                   /* title bar */
    TEST_ASSERT_EQUAL_HEX16(GV_RGB565(44, 154, 79), banner_px(fb));                /* green NORMAL */
    for (int y = 0; y < GV_H; y++) for (int x = 0; x < GV_W; x++)
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0xAAAA, fb[y][x], "pixel never painted");
}

static void test_banner_follows_battery_state_and_silence(void) {
    struct { gv_inputs_t in; uint16_t col; } cases[6];
    for (int i = 0; i < 6; i++) cases[i].in = normal();
    cases[0].in.battery_state = FB_ST_WARNING; cases[0].col = GV_RGB565(231, 160, 32);
    cases[1].in.battery_state = FB_ST_FAULT;   cases[1].col = GV_RGB565(226, 75, 74);
    cases[2].in.battery_state = FB_ST_SAFE;    cases[2].col = GV_RGB565(226, 75, 74);
    cases[3].in.battery_alive = 0;             cases[3].col = GV_RGB565(226, 75, 74);   /* BATTERY SILENT */
    cases[4].in.have = 0; cases[4].in.battery_alive = 0; cases[4].col = GV_RGB565(90, 90, 90);   /* NO DATA */
    cases[5].in.limp_home = 1;                 cases[5].col = GV_RGB565(231, 160, 32);  /* recovering: LIMP HOME */
    for (int i = 0; i < 6; i++) { gv_draw(&g, &cases[i].in); TEST_ASSERT_EQUAL_HEX16_MESSAGE(cases[i].col, banner_px(fb), "banner colour"); }
}

static void test_needle_angle_mapping_and_clamping(void) {
    TEST_ASSERT_EQUAL(0, gv_volt_to_deg(0));
    TEST_ASSERT_EQUAL(0, gv_volt_to_deg(2800));
    TEST_ASSERT_EQUAL(90, gv_volt_to_deg(3500));
    TEST_ASSERT_EQUAL(180, gv_volt_to_deg(4200));
    TEST_ASSERT_EQUAL(180, gv_volt_to_deg(100000));
    gv_inputs_t in = normal(); in.pack_mv = -5; in.ma = -99999; in.temp_dc = 32767; in.soc_pct = 255;
    gv_draw(&g, &in);                                                              /* extremes stay on screen (tearDown) */
}

/* The incremental redraw must always end in exactly the picture a fresh full draw would give. */
static void test_incremental_updates_match_a_fresh_draw(void) {
    srand(7);
    gv_inputs_t in = normal();
    for (int step = 0; step < 300; step++) {
        in.have = rand() % 10 != 0;
        in.pack_mv = 2600 + rand() % 1800;
        in.ma = (rand() % 6000) - 3000;
        in.temp_dc = (rand() % 900) - 100;
        in.soc_pct = (uint8_t)(rand() % 101);
        in.battery_alive = rand() % 8 != 0;
        in.battery_state = (fb_state_t)(rand() % 5);
        in.limp_home = rand() % 4 == 0;
        gv_draw(&g, &in);
        if (step % 25 == 24) {
            gv_t fresh; memset(ref, 0x55, sizeof ref);
            gv_init(&fresh, ops_for(ref)); gv_draw(&fresh, &in);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(ref, fb, sizeof fb, "incremental redraw drifted from a full redraw");
        }
    }
}

static void test_unchanged_inputs_cost_almost_nothing(void) {
    gv_inputs_t in = normal(); gv_draw(&g, &in);
    int first = calls; calls = 0;
    gv_draw(&g, &in);
    TEST_ASSERT_TRUE(first > 300);
    TEST_ASSERT_LESS_OR_EQUAL(1, calls);                                           /* just the hub */
}

/* The body node feeds the cluster from what it hears on the bus: real node core on the host simulator. */
static void test_body_node_feeds_the_cluster(void) {
    uint8_t sk[64], pk[32], img[8192], body[3000]; test_keypair(sk, pk); memset(body, 0xA0, sizeof body); body[0] = 0;
    sim_node_t *bat = sim_node_new(FB_NODE_BATTERY, pk), *bod = sim_node_new(FB_NODE_BODY, pk);
    size_t n = test_build_image(img, sizeof img, sk, 1, 0, FB_NODE_BATTERY, FB_BOARD_HW_ID, body, sizeof body); sim_node_provision(bat, 0, img, (uint32_t)n);
    n = test_build_image(img, sizeof img, sk, 1, 0, FB_NODE_BODY, FB_BOARD_HW_ID, body, sizeof body); sim_node_provision(bod, 0, img, (uint32_t)n);
    uint32_t now = 1000; sim_node_power_on(bat, now); sim_node_power_on(bod, now);
    for (int t = 0; t < 300; t++) {
        now += 10; sim_node_tick(bat, now, 3150, 700, 300, 1); sim_node_tick(bod, now, 3800, 0, 250, 1);
        uint32_t id; uint8_t d[8], dlc;
        while (sim_node_pop_tx(bat, &id, d, &dlc)) sim_node_rx(bod, now, id, d, dlc);
        while (sim_node_pop_tx(bod, &id, d, &dlc)) sim_node_rx(bat, now, id, d, dlc);
    }
    gv_inputs_t in; sim_node_gauge_inputs(bod, now, &in);
    TEST_ASSERT_EQUAL(1, in.have); TEST_ASSERT_EQUAL_INT32(3150, in.pack_mv); TEST_ASSERT_EQUAL(1, in.battery_alive);
    TEST_ASSERT_EQUAL(FB_ST_WARNING, in.battery_state);                             /* 3.15 V is below the 3.3 V warning */
    gv_draw(&g, &in);
    TEST_ASSERT_EQUAL_HEX16(GV_RGB565(231, 160, 32), banner_px(fb));
    sim_node_free(bat); sim_node_free(bod);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_draw_paints_title_and_normal_banner); RUN_TEST(test_banner_follows_battery_state_and_silence);
    RUN_TEST(test_needle_angle_mapping_and_clamping); RUN_TEST(test_incremental_updates_match_a_fresh_draw);
    RUN_TEST(test_unchanged_inputs_cost_almost_nothing); RUN_TEST(test_body_node_feeds_the_cluster);
    return UNITY_END();
}
