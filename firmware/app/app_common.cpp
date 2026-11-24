#include "app_common.hpp"

namespace fleetbus::app {

void CycleTimer::begin(Status& st) {
    t0_ = bsp_micros();
    grid_us_ += kTickMs * 1000u;
    const uint32_t late = t0_ - grid_us_;
    if (late < 0x80000000u && late > st.late_us_max) st.late_us_max = late;   // ignore "early" (tick/SysTick skew)
}

void CycleTimer::end(Status& st) const {
    const uint32_t cycle = bsp_micros() - t0_;
    if (cycle > st.cycle_us_max) st.cycle_us_max = cycle;
}

void send_all(NodeBase& node, Status& st) {
    CanFrame f;
    while (node.pop_tx(f))
        if (bsp_can_send(f.id, f.data.data(), f.dlc) != 0) st.tx_send_fail++;   // TX FIFO full or bus-off
}

void log_status(const Status& s) {
    static const char* const kState[] = {"BOOT", "NORMAL", "WARNING", "FAULT", "SAFE", "LIMP_HOME"};
    bsp_log("t=%lu %s peer=%d rx=%lu can_err=%lu tx_drop=%lu tx_fail=%lu node_us_max=%lu late_us_max=%lu",
            static_cast<unsigned long>(bsp_millis()), s.state <= FB_ST_LIMP_HOME ? kState[s.state] : "?",
            s.peer_alive ? 1 : 0, static_cast<unsigned long>(s.rx_frames), static_cast<unsigned long>(s.can_errors),
            static_cast<unsigned long>(s.tx_dropped), static_cast<unsigned long>(s.tx_send_fail),
            static_cast<unsigned long>(s.cycle_us_max), static_cast<unsigned long>(s.late_us_max));
}

void log_stacks_and_heap(const TaskHandle_t* tasks, int n) {
    bsp_log(" stk=");
    for (int i = 0; i < n; i++)
        bsp_log("%lu,", static_cast<unsigned long>(uxTaskGetStackHighWaterMark(tasks[i])) * 4u);   // free bytes
    bsp_log(" heap=%lu\r\n", static_cast<unsigned long>(xPortGetFreeHeapSize()));
}

void create_task(TaskFunction_t fn, const char* name, uint16_t stack_words, UBaseType_t prio, TaskHandle_t* out) {
    if (xTaskCreate(fn, name, stack_words, nullptr, prio, out) != pdPASS) {
        bsp_log("task %s: out of heap\r\n", name);
        for (;;) {}                                         // the watchdog resets the board
    }
}

}  // namespace fleetbus::app

extern "C" {
void vApplicationMallocFailedHook(void) { bsp_log("malloc failed\r\n"); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t, char* name) { bsp_log("stack overflow %s\r\n", name); for (;;) {} }
// Called if a pure virtual function is ever invoked (it can't be here). Defined so libsupc++'s version, which
// drags in abort() and the exception machinery, isn't linked.
void __cxa_pure_virtual() { for (;;) {} }
}
