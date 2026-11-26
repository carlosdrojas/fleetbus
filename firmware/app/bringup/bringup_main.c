/* Board bring-up (P2): the first thing to flash on a new board. Two FreeRTOS tasks:
 *   blink: LED toggles every 250 ms (proves the clock, GPIO and the scheduler)
 *   log:   once a second prints uptime and a counter on the log UART (proves UART baud rate and SysTick rate)
 * Measure on the bench: LED period with a scope (expect 500 ms), uptime vs a stopwatch over 60 s (tick accuracy).
 * Board-neutral: uses only bsp.h. Linked at the start of flash. Ran on both boards 2026-11-24/25. */
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"

static void blink_task(void *arg) {
    (void)arg;
    for (int on = 0;; on = !on) { bsp_led(on); vTaskDelay(pdMS_TO_TICKS(250)); }
}

static void log_task(void *arg) {
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (uint32_t n = 0;; n++) {
        bsp_log("bringup %s t=%lu ms n=%lu free_heap=%lu\r\n", FB_BOARD_NAME, (unsigned long)bsp_millis(),
                (unsigned long)n, (unsigned long)xPortGetFreeHeapSize());
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

void vApplicationMallocFailedHook(void) { bsp_log("malloc failed\r\n"); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t t, char *name) { (void)t; bsp_log("stack overflow %s\r\n", name); for (;;) {} }

int main(void) {
    bsp_board_init();
    bsp_log_init();
    bsp_log("\r\nFleetBus bring-up on %s\r\n", FB_BOARD_NAME);
    xTaskCreate(blink_task, "blink", 128, 0, 1, 0);
    xTaskCreate(log_task, "log", 256, 0, 2, 0);
    vTaskStartScheduler();
    for (;;) {}
}
