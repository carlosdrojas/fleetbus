// Body-controller ECU. Three FreeRTOS tasks:
//   node  prio 2  every 10 ms   CAN in -> limp-home FSM -> lights + servo -> CAN out -> feed the watchdog
//   disp  prio 1  every 200 ms  redraw the instrument cluster over SPI (can take milliseconds)
//   log   prio 1  every 1 s     status line on the UART (polled, ~7 ms per line)
// The slow work (SPI redraw, UART) runs below the node task, so it can never delay a CAN cycle. The node task
// prints its own worst run time and start lateness, so that's measured on the bench, not assumed.
//
// Outputs: head/turn lights = LaunchPad LED2 green/blue, fault light = LED1 red, servo on PB4 (powered from its own
// 5 V supply, grounds joined). Pins: bsp/mspm0g3507/bsp_pins.h.
#include "app_common.hpp"
#include "gauge_view.hpp"

using namespace fleetbus;
using namespace fleetbus::app;

namespace {

BodyNode g_node{FB_FW_VERSION};       // constexpr constructor: built at compile time, no startup code runs for it
Latest<Status> g_status;
Latest<GaugeInputs> g_gauge;          // node -> disp
Latest<uint32_t> g_disp_us;           // disp -> log: last redraw time
TaskHandle_t g_tasks[3];

void node_task(void*) {
    g_node.start(bsp_millis());
    Status st;
    CycleTimer timer;
    TickType_t last = xTaskGetTickCount();
    timer.start();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(kTickMs));
        timer.begin(st);
        const uint32_t now = bsp_millis();

        CanFrame f;
        for (int budget = 32; budget > 0 && bsp_can_receive(&f.id, f.data.data(), &f.dlc); budget--) {
            g_node.on_can(now, f);
            st.rx_frames++;
        }
        g_node.tick(now);
        bsp_lights_set(g_node.outputs().lights);       // same cycle as the decision: no extra latency
        bsp_servo_set_deg(g_node.outputs().servo_deg);
        send_all(g_node, st);

        st.state = g_node.state();
        st.peer_alive = g_node.peer_alive(now);
        st.can_errors = bsp_can_error_count();
        st.tx_dropped = g_node.tx_dropped();
        timer.end(st);
        g_status.store(st);
        g_gauge.store(g_node.gauge_inputs(now));
        bsp_wdt_feed();               // a hung node task stops feeding: the board resets within 2 s
    }
}

// The ST7735 behind the cluster. GaugeView only repaints what changed, so a steady screen costs one small fill.
struct LcdCanvas final : Canvas {
    void fill(int x, int y, int w, int h, uint16_t c) override { bsp_display_fill(x, y, w, h, c); }
};

void display_task(void*) {
    if (bsp_display_init() != 0) bsp_log("display: no driver\r\n");
    LcdCanvas lcd;
    GaugeView view{lcd};
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(200));
        const uint32_t t0 = bsp_micros();
        view.draw(g_gauge.load());
        g_disp_us.store(bsp_micros() - t0);
    }
}

void log_task(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
        log_status(g_status.load());
        bsp_log(" disp_us=%lu", static_cast<unsigned long>(g_disp_us.load()));
        log_stacks_and_heap(g_tasks, 3);
    }
}

}  // namespace

int main() {
    bsp_board_init();
    bsp_log_init();
    bsp_wdt_start_2s();               // can't be stopped once started
    bsp_log("\r\nFleetBus body node, fw v%d, C++17\r\n", FB_FW_VERSION);
    bsp_servo_init();
    if (bsp_can_init() != 0) bsp_log("CAN init failed\r\n");
    create_task(node_task, "node", 384, 2, &g_tasks[0]);   // stack sizes in words; "stk" prints what's left
    create_task(display_task, "disp", 384, 1, &g_tasks[1]);
    create_task(log_task, "log", 256, 1, &g_tasks[2]);
    vTaskStartScheduler();
    for (;;) {}
}
