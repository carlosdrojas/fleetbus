#pragma once
// What both node images share. Each board has its own main: battery_main.cpp and body_main.cpp.
#include <cstdint>
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"
#include "node.hpp"

namespace fleetbus::app {

// Interrupts off (PRIMASK on the M0+) from construction to the end of the scope, so no return path can forget to
// turn them back on. Only ever wraps a copy of a few dozen bytes.
class CriticalSection {
public:
    CriticalSection() { taskENTER_CRITICAL(); }
    ~CriticalSection() { taskEXIT_CRITICAL(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};

// A variable shared between tasks. The writer overwrites it, readers get a whole copy, nobody sees half an update.
// Readers want the newest value, not a backlog, so this is a mailbox, not a queue.
template <typename T>
class Latest {
public:
    void store(const T& v) { CriticalSection cs; value_ = v; }
    T load() const { CriticalSection cs; return value_; }
private:
    T value_{};
};

// What the log task prints. The node task writes it once per cycle.
struct Status {
    fb_state_t state = FB_ST_BOOT;
    bool peer_alive = false;
    uint32_t rx_frames = 0, can_errors = 0, tx_dropped = 0, tx_send_fail = 0;
    uint32_t cycle_us_max = 0;   // worst node-task run time since boot
    uint32_t late_us_max = 0;    // worst node-task start lateness against its 10 ms grid since boot
};

// Times the 10 ms node task. begin() at the top of each cycle, end() at the bottom.
class CycleTimer {
public:
    void start() { grid_us_ = bsp_micros(); }
    void begin(Status& st);
    void end(Status& st) const;
private:
    uint32_t grid_us_ = 0, t0_ = 0;
};

void send_all(NodeBase& node, Status& st);                 // node's TX queue -> CAN controller
void log_status(const Status& st);                         // the common part of the 1 s status line
void log_stacks_and_heap(const TaskHandle_t* tasks, int n);  // ... and its end, with "\r\n"
void create_task(TaskFunction_t fn, const char* name, uint16_t stack_words, UBaseType_t prio, TaskHandle_t* out);

}  // namespace fleetbus::app
