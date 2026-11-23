// FleetBus node application: FreeRTOS tasks around the portable node logic (common/node.cpp).
// One image per role (app.hpp). The tasks share data only through Latest<T> snapshots and check-in counters.
//
//   task   prio  period  does
//   wdg    4     100 ms  feeds the 2 s watchdog only if every other task checked in since the last look
//   node   3     10 ms   drain CAN RX -> node logic -> drain TX queue -> CAN; body: apply outputs
//   sense  2     20 ms   battery only: read ADC (+ INA219), publish the latest sample
//   disp   1     200 ms  body only: redraw the instrument cluster over SPI from the latest snapshot
//   log    1     1 s     status line on the UART (polled, ~7 ms per line: kept out of the node task)
//
// Rate-monotonic order: the shorter the period, the higher the priority. The watchdog task sits on top so a
// runaway lower task can't starve the feed decision (it decides, it doesn't do work).
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "FreeRTOS.h"
#include "task.h"
#include "app.hpp"
#include "bsp.h"

#ifndef FB_FW_VERSION
#define FB_FW_VERSION 1
#endif
#ifndef FB_HAS_DISPLAY
#define FB_HAS_DISPLAY 0
#endif

namespace {
using namespace fleetbus;

constexpr bool kBattery = std::is_same_v<app::Node, BatteryNode>;
constexpr bool kDisplay = FB_HAS_DISPLAY != 0 && !kBattery;

// Stack depths in words (4 B). Each is printed as a high-water mark ("stk") so they can be cut to measured need.
constexpr uint16_t kNodeStack = 384, kSenseStack = 256, kWdgStack = 160, kDispStack = 384, kLogStack = 256;

// RAII critical section: interrupts masked (PRIMASK on the M0+) from construction to the end of the scope.
// Only ever wraps a copy of a few dozen bytes.
class CriticalSection {
public:
    CriticalSection() noexcept { taskENTER_CRITICAL(); }
    ~CriticalSection() { taskEXIT_CRITICAL(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};

// Latest-value mailbox between tasks. The writer overwrites, readers copy; nobody ever sees a half-written value.
// A queue would be wrong here: readers want the newest sample, not a backlog of old ones.
template <typename T>
class Latest {
public:
    void store(const T& v) noexcept { CriticalSection cs; value_ = v; }
    [[nodiscard]] T load() const noexcept { CriticalSection cs; return value_; }
private:
    T value_{};
};

// Watchdog check-in. One writer per counter, so a relaxed load + store is enough: the M0+ has no atomic
// read-modify-write instructions, and fetch_add would pull in a libatomic call that disables interrupts anyway.
class CheckIn {
public:
    void beat() noexcept { n_.store(n_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }
    // True if beat() ran since the previous call.
    [[nodiscard]] bool progressed() noexcept {
        const uint32_t now = n_.load(std::memory_order_relaxed);
        const bool moved = now != seen_;
        seen_ = now;
        return moved;
    }
private:
    std::atomic<uint32_t> n_{0};
    uint32_t seen_ = 0;
};

// What the log task prints. Written by the node task once per cycle.
struct Status {
    fb_state_t state = FB_ST_BOOT;
    bool peer_alive = false;
    uint32_t can_errors = 0, tx_dropped = 0, tx_send_fail = 0, rx_frames = 0;
    uint32_t cycle_us_max = 0;     // node task execution time, worst case since boot
    uint32_t late_us_max = 0;      // node task start lateness vs its 10 ms grid, worst case since boot
};

app::Node g_node{FB_FW_VERSION};   // constexpr constructor: constant-initialized, no startup code runs for it
Latest<Status> g_status;
Latest<BatterySample> g_sample;    // battery: sense -> node
Latest<GaugeInputs> g_gauge;       // body: node -> disp
Latest<uint32_t> g_disp_us;        // body: last full draw time
CheckIn g_chk_node, g_chk_sense, g_chk_disp;
TaskHandle_t g_task[5];

// Role-specific parts of a cycle. Templates, so the other role's branch is discarded and never compiled into the image.
template <typename N>
void step(N& node, uint32_t now) {
    if constexpr (std::is_same_v<N, BatteryNode>) {
        node.tick(now, g_sample.load());
    } else {
        node.tick(now);
        app::apply(node.outputs());                 // same cycle as the decision: no extra latency
        if constexpr (kDisplay) g_gauge.store(node.gauge_inputs(now));
    }
}
template <typename N>
void sense_once() {
    if constexpr (std::is_same_v<N, BatteryNode>) g_sample.store(app::read_sample());
}

void node_task(void*) {
    g_node.start(bsp_millis());
    Status st;
    TickType_t last = xTaskGetTickCount();
    uint32_t grid_us = bsp_micros();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(kTickMs));
        const uint32_t t0 = bsp_micros();
        grid_us += kTickMs * 1000u;
        const uint32_t late = t0 - grid_us;
        if (late < 0x80000000u && late > st.late_us_max) st.late_us_max = late;   // ignore "early" (tick/SysTick skew)
        const uint32_t now = bsp_millis();

        CanFrame f;
        for (int budget = 32; budget > 0 && bsp_can_receive(&f.id, f.data.data(), &f.dlc); budget--) {
            g_node.on_can(now, f);
            st.rx_frames++;
        }
        step(g_node, now);
        while (g_node.pop_tx(f)) {
            if (bsp_can_send(f.id, f.data.data(), f.dlc) != 0) st.tx_send_fail++;   // TX FIFO full or bus-off
        }

        st.state = g_node.state();
        st.peer_alive = g_node.peer_alive(now);
        st.can_errors = bsp_can_error_count();
        st.tx_dropped = g_node.tx_dropped();
        const uint32_t cycle = bsp_micros() - t0;
        if (cycle > st.cycle_us_max) st.cycle_us_max = cycle;
        g_status.store(st);
        g_chk_node.beat();
    }
}

void sense_task(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
        sense_once<app::Node>();
        g_chk_sense.beat();
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
        g_chk_disp.beat();
    }
}

void wdg_task(void*) {
    uint32_t disp_seen_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        bool ok = g_chk_node.progressed();                       // every 10 ms, so it must move every 100 ms
        if constexpr (kBattery) ok = g_chk_sense.progressed() && ok;
        if constexpr (kDisplay) {                                // draws every 200 ms: allow 1 s without progress
            const uint32_t now = bsp_millis();
            if (g_chk_disp.progressed() || disp_seen_ms == 0) disp_seen_ms = now;
            ok = ok && now - disp_seen_ms < 1000u;
        }
        if (ok) bsp_wdt_feed();                                  // otherwise the WWDT resets the board within 2 s
    }
}

void log_task(void*) {
    static const char* const kState[] = {"BOOT", "NORMAL", "WARNING", "FAULT", "SAFE", "LIMP_HOME"};
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
        const Status s = g_status.load();
        bsp_log("t=%lu %s peer=%d rx=%lu can_err=%lu tx_drop=%lu tx_fail=%lu node_us_max=%lu late_us_max=%lu",
                static_cast<unsigned long>(bsp_millis()), s.state <= FB_ST_LIMP_HOME ? kState[s.state] : "?",
                s.peer_alive ? 1 : 0, static_cast<unsigned long>(s.rx_frames), static_cast<unsigned long>(s.can_errors),
                static_cast<unsigned long>(s.tx_dropped), static_cast<unsigned long>(s.tx_send_fail),
                static_cast<unsigned long>(s.cycle_us_max), static_cast<unsigned long>(s.late_us_max));
        if constexpr (kBattery) {
            const BatterySample b = g_sample.load();
            bsp_log(" mv=%ld ma=%ld dc=%ld ok=%d", static_cast<long>(b.pack_mv), static_cast<long>(b.ma),
                    static_cast<long>(b.temp_dc), b.sensor_ok ? 1 : 0);
        }
        if constexpr (kDisplay) bsp_log(" disp_us=%lu", static_cast<unsigned long>(g_disp_us.load()));
        bsp_log(" stk=");
        for (TaskHandle_t t : g_task)
            if (t) bsp_log("%lu,", static_cast<unsigned long>(uxTaskGetStackHighWaterMark(t)) * 4u);
        bsp_log(" heap=%lu\r\n", static_cast<unsigned long>(xPortGetFreeHeapSize()));
    }
}

void create(TaskFunction_t fn, const char* name, uint16_t words, UBaseType_t prio, TaskHandle_t* out) {
    if (xTaskCreate(fn, name, words, nullptr, prio, out) != pdPASS) {
        bsp_log("task %s: out of heap\r\n", name);
        for (;;) {}                                              // the watchdog resets us
    }
}

}  // namespace

extern "C" {
void vApplicationMallocFailedHook(void) { bsp_log("malloc failed\r\n"); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t, char* name) { bsp_log("stack overflow %s\r\n", name); for (;;) {} }
// Called if a pure virtual function is ever invoked (it can't be here). Defined so libsupc++'s version, which
// drags in abort() and the exception machinery, isn't linked.
void __cxa_pure_virtual() { for (;;) {} }
}

int main() {
    bsp_board_init();
    bsp_log_init();
    bsp_wdt_start_2s();
    bsp_log("\r\nFleetBus %s node, fw v%d, C++17\r\n", kBattery ? "battery" : "body", FB_FW_VERSION);
    app::hw_init();
    if (bsp_can_init() != 0) bsp_log("CAN init failed\r\n");
    create(wdg_task, "wdg", kWdgStack, 4, &g_task[0]);
    create(node_task, "node", kNodeStack, 3, &g_task[1]);
    if constexpr (kBattery) create(sense_task, "sense", kSenseStack, 2, &g_task[2]);
    if constexpr (kDisplay) create(display_task, "disp", kDispStack, 1, &g_task[3]);
    create(log_task, "log", kLogStack, 1, &g_task[4]);
    vTaskStartScheduler();
    for (;;) {}
}
