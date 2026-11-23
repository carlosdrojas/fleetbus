// Body-node instrument cluster (common/gauge_view.cpp) against an in-memory 160x128 RGB565 framebuffer.
#include <cstdlib>
#include <cstring>
#include "unity.h"
#include "gauge_view.hpp"
#include "sim_bus.hpp"

using namespace fleetbus;

namespace {
struct Framebuffer final : Canvas {
    uint16_t px[kLcdH][kLcdW];
    int calls = 0, out_of_bounds = 0;
    explicit Framebuffer(uint8_t fillbyte) { std::memset(px, fillbyte, sizeof px); }
    void fill(int x, int y, int w, int h, uint16_t c) override {
        calls++;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > kLcdW || y + h > kLcdH) { out_of_bounds++; return; }
        for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px[j][i] = c;
    }
    uint16_t banner_px() const { return px[kLcdH - 2][2]; }   // banner corner, never text
};
GaugeInputs normal() {
    GaugeInputs in;
    in.have = true; in.pack_mv = 3712; in.ma = 842; in.temp_dc = 251; in.soc_pct = 59;
    in.battery_alive = true; in.battery_state = FB_ST_NORMAL; in.limp_home = false;
    return in;
}
}  // namespace

static Framebuffer* fb;
static GaugeView* g;
void setUp() { fb = new Framebuffer(0xAA); g = new GaugeView(*fb); }
void tearDown() { TEST_ASSERT_EQUAL_MESSAGE(0, fb->out_of_bounds, "drew outside the 160x128 screen"); delete g; delete fb; }

static void test_first_draw_paints_title_and_normal_banner() {
    g->draw(normal());
    TEST_ASSERT_EQUAL_HEX16(rgb565(31, 95, 168), fb->px[1][150]);   // title bar
    TEST_ASSERT_EQUAL_HEX16(rgb565(44, 154, 79), fb->banner_px());  // green NORMAL
    for (auto& row : fb->px) for (uint16_t c : row) TEST_ASSERT_NOT_EQUAL_MESSAGE(0xAAAA, c, "pixel never painted");
}

static void test_banner_follows_battery_state_and_silence() {
    struct Case { GaugeInputs in; uint16_t col; } cases[6];
    for (auto& c : cases) c.in = normal();
    cases[0].in.battery_state = FB_ST_WARNING; cases[0].col = rgb565(231, 160, 32);
    cases[1].in.battery_state = FB_ST_FAULT;   cases[1].col = rgb565(226, 75, 74);
    cases[2].in.battery_state = FB_ST_SAFE;    cases[2].col = rgb565(226, 75, 74);
    cases[3].in.battery_alive = false;         cases[3].col = rgb565(226, 75, 74);   // BATTERY SILENT
    cases[4].in.have = false; cases[4].in.battery_alive = false; cases[4].col = rgb565(90, 90, 90);   // NO DATA
    cases[5].in.limp_home = true;              cases[5].col = rgb565(231, 160, 32);  // recovering: LIMP HOME
    for (auto& c : cases) { g->draw(c.in); TEST_ASSERT_EQUAL_HEX16_MESSAGE(c.col, fb->banner_px(), "banner colour"); }
}

static void test_needle_angle_mapping_and_clamping() {
    TEST_ASSERT_EQUAL(0, GaugeView::volt_to_deg(0));
    TEST_ASSERT_EQUAL(0, GaugeView::volt_to_deg(2800));
    TEST_ASSERT_EQUAL(90, GaugeView::volt_to_deg(3500));
    TEST_ASSERT_EQUAL(180, GaugeView::volt_to_deg(4200));
    TEST_ASSERT_EQUAL(180, GaugeView::volt_to_deg(100000));
    GaugeInputs in = normal(); in.pack_mv = -5; in.ma = -99999; in.temp_dc = 32767; in.soc_pct = 255;
    g->draw(in);                                                    // extremes stay on screen (tearDown)
}

// The incremental redraw must always end in exactly the picture a fresh full draw would give.
static void test_incremental_updates_match_a_fresh_draw() {
    std::srand(7);
    GaugeInputs in = normal();
    for (int step = 0; step < 300; step++) {
        in.have = std::rand() % 10 != 0;
        in.pack_mv = 2600 + std::rand() % 1800;
        in.ma = (std::rand() % 6000) - 3000;
        in.temp_dc = (std::rand() % 900) - 100;
        in.soc_pct = static_cast<uint8_t>(std::rand() % 101);
        in.battery_alive = std::rand() % 8 != 0;
        in.battery_state = static_cast<fb_state_t>(std::rand() % 5);
        in.limp_home = std::rand() % 4 == 0;
        g->draw(in);
        if (step % 25 == 24) {
            Framebuffer ref(0x55);
            GaugeView fresh{ref};
            fresh.draw(in);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(ref.px, fb->px, sizeof fb->px, "incremental redraw drifted from a full redraw");
        }
    }
}

static void test_unchanged_inputs_cost_almost_nothing() {
    g->draw(normal());
    const int first = fb->calls; fb->calls = 0;
    g->draw(normal());
    TEST_ASSERT_TRUE(first > 300);
    TEST_ASSERT_LESS_OR_EQUAL(1, fb->calls);                        // just the hub
}

// The body node feeds the cluster from what it hears on the bus.
static void test_body_node_feeds_the_cluster() {
    fleetbus::test::SimBus bus;
    bus.sample = BatterySample{3150, 700, 300, true};
    bus.run(3000);
    const GaugeInputs in = bus.body.gauge_inputs(bus.now);
    TEST_ASSERT_TRUE(in.have); TEST_ASSERT_EQUAL_INT32(3150, in.pack_mv); TEST_ASSERT_TRUE(in.battery_alive);
    TEST_ASSERT_EQUAL(FB_ST_WARNING, in.battery_state);             // 3.15 V is below the 3.3 V warning
    g->draw(in);
    TEST_ASSERT_EQUAL_HEX16(rgb565(231, 160, 32), fb->banner_px());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_draw_paints_title_and_normal_banner); RUN_TEST(test_banner_follows_battery_state_and_silence);
    RUN_TEST(test_needle_angle_mapping_and_clamping); RUN_TEST(test_incremental_updates_match_a_fresh_draw);
    RUN_TEST(test_unchanged_inputs_cost_almost_nothing); RUN_TEST(test_body_node_feeds_the_cluster);
    return UNITY_END();
}
