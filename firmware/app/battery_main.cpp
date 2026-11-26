// Battery-monitor ECU. Two FreeRTOS tasks:
//   node  prio 2  every 10 ms  read the ADC -> CAN in -> fault FSM -> CAN out -> feed the watchdog
//   log   prio 1  every 1 s    status line on the UART (polled, ~7 ms per line, so it stays out of the node task)
//
// Bench stand-ins for a real cell:
//   pack voltage  potentiometer on PA24, 0-3.3 V mapped to 3.0-4.2 V (turn it down to trip undervoltage)
//   temperature   10 k resistor from 3V3 to PA25, 10 k NTC from PA25 to GND; ESTIMATED lookup curve (ntc_lut.inc)
//   current       no sensor fitted: always 0 mA, so overcurrent can't fire on this bench
#include <iterator>
#include "app_common.hpp"

using namespace fleetbus;
using namespace fleetbus::app;

namespace {

struct NtcPoint { int16_t ratio_milli; int16_t dc; };
constexpr NtcPoint kNtc[] = {
#include "ntc_lut.inc"
};

// Divider ratio falls as temperature rises. Linear interpolation between table points, clamped at both ends.
int32_t ntc_to_dc(int32_t ratio_milli) {
    constexpr auto n = std::size(kNtc);
    if (ratio_milli >= kNtc[0].ratio_milli) return kNtc[0].dc;
    if (ratio_milli <= kNtc[n - 1].ratio_milli) return kNtc[n - 1].dc;
    for (std::size_t i = 0; i + 1 < n; i++) {
        const NtcPoint a = kNtc[i], b = kNtc[i + 1];
        if (ratio_milli <= a.ratio_milli && ratio_milli > b.ratio_milli)
            return a.dc + (b.dc - a.dc) * (a.ratio_milli - ratio_milli) / (a.ratio_milli - b.ratio_milli);
    }
    return kNtc[0].dc;
}

BatterySample read_sample() {
    const int t_mv = bsp_adc_read_mv(1), p_mv = bsp_adc_read_mv(2);   // -1 on ADC timeout
    BatterySample s;
    s.sensor_ok = t_mv >= 0 && p_mv >= 0;
    s.temp_dc = t_mv >= 0 ? ntc_to_dc(t_mv * 1000 / 3300) : 0;
    // Pot 0..3300 mV -> "pack" 2800..4200 mV, the gauge's scale. Was 3000..4200: the pot's end stop sat exactly on the
    // 3.000 V FAULT threshold, so FAULT only just tripped and SAFE took ~3 s instead of ~1 s on the bench (2026-11-25).
    // Now FAULT trips at ~471 mV on the wiper (14 % of travel) and WARNING at ~943 mV.
    s.pack_mv = p_mv >= 0 ? 2800 + p_mv * 1400 / 3300 : 0;
    s.ma = 0;
    return s;
}

BatteryNode g_node{FB_FW_VERSION};    // constexpr constructor: built at compile time, no startup code runs for it
Latest<Status> g_status;
Latest<BatterySample> g_sample;       // node -> log
TaskHandle_t g_tasks[2];

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
        const BatterySample sample = read_sample();

        CanFrame f;
        for (int budget = 32; budget > 0 && bsp_can_receive(&f.id, f.data.data(), &f.dlc); budget--) {
            g_node.on_can(now, f);
            st.rx_frames++;
        }
        g_node.tick(now, sample);
        send_all(g_node, st);

        st.state = g_node.state();
        st.peer_alive = g_node.peer_alive(now);
        st.can_errors = bsp_can_error_count();
        st.tx_dropped = g_node.tx_dropped();
        timer.end(st);
        g_status.store(st);
        g_sample.store(sample);
        bsp_wdt_feed();               // a hung node task stops feeding: the board resets within 2 s
    }
}

void log_task(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
        log_status(g_status.load());
        const BatterySample b = g_sample.load();
        bsp_log(" mv=%ld ma=%ld dc=%ld ok=%d", static_cast<long>(b.pack_mv), static_cast<long>(b.ma),
                static_cast<long>(b.temp_dc), b.sensor_ok ? 1 : 0);
        log_stacks_and_heap(g_tasks, 2);
    }
}

}  // namespace

int main() {
    bsp_board_init();
    bsp_log_init();
    bsp_wdt_start_2s();               // can't be stopped once started
    bsp_log("\r\nFleetBus battery node, fw v%d, C++17\r\n", FB_FW_VERSION);
    bsp_adc_init();
    if (bsp_can_init() != 0) bsp_log("CAN init failed\r\n");
    create_task(node_task, "node", 384, 2, &g_tasks[0]);   // stack sizes in words; "stk" prints what's left
    create_task(log_task, "log", 256, 1, &g_tasks[1]);
    vTaskStartScheduler();
    for (;;) {}
}
