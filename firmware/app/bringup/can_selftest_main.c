/* CAN self-test (P3). Bench only: never connect this image to the car.
 *   1. Internal loopback: sends 100 frames with id 0x7F0 and a counter, checks each comes back unchanged. Prints PASS/FAIL.
 *   2. Bus mode: leaves loopback, sends one 0x7F0 counter frame every 100 ms, prints every frame received.
 *      With two LaunchPads (both running this) on one terminated bus, each must print the other's counter;
 *      with the CANable, `candump can0` must show 0x7F0 at 10 Hz from each node.
 * Board-neutral (bsp.h only). NOT YET RUN ON A BOARD. */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"

#define TEST_ID 0x7F0u

static int loopback_test(void) {
    if (bsp_can_set_loopback(1) != 0) { bsp_log("loopback not supported on %s\r\n", FB_BOARD_NAME); return -1; }
    int ok = 0;
    for (uint32_t n = 0; n < 100; n++) {
        uint8_t tx[8] = { (uint8_t)n, (uint8_t)(n >> 8), 0xA5, 0x5A, 1, 2, 3, 4 }, rx[8]; uint32_t id; uint8_t dlc;
        if (bsp_can_send(TEST_ID, tx, 8) != 0) continue;
        int got = 0;
        for (int t = 0; t < 20 && !got; t++) { got = bsp_can_receive(&id, rx, &dlc); if (!got) vTaskDelay(1); }
        if (got && id == TEST_ID && dlc == 8 && memcmp(tx, rx, 8) == 0) ok++;
    }
    bsp_can_set_loopback(0);
    bsp_log("CAN loopback: %d/100 %s, errors=%lu\r\n", ok, ok == 100 ? "PASS" : "FAIL", (unsigned long)bsp_can_error_count());
    return ok == 100 ? 0 : -1;
}

static void can_task(void *arg) {
    (void)arg;
    loopback_test();
    TickType_t last = xTaskGetTickCount();
    for (uint32_t n = 0;; n++) {
        uint8_t d[8] = { (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) };
        if (bsp_can_send(TEST_ID, d, 4) != 0) bsp_log("tx fail n=%lu\r\n", (unsigned long)n);
        uint32_t id; uint8_t rx[8], dlc;
        while (bsp_can_receive(&id, rx, &dlc))
            bsp_log("rx id=%x dlc=%u n=%lu\r\n", (unsigned)id, (unsigned)dlc,
                    (unsigned long)(rx[0] | rx[1] << 8 | (uint32_t)rx[2] << 16 | (uint32_t)rx[3] << 24));
        bsp_led(n & 1);
        if (n % 50 == 0) bsp_log("t=%lu sent=%lu can_err=%lu\r\n", (unsigned long)bsp_millis(), (unsigned long)n,
                                 (unsigned long)bsp_can_error_count());
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}

void vApplicationMallocFailedHook(void) { for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t t, char *name) { (void)t; bsp_log("stack overflow %s\r\n", name); for (;;) {} }

int main(void) {
    bsp_board_init();
    bsp_log_init();
    int r = bsp_can_init();
    bsp_log("\r\nFleetBus CAN self-test on %s: init %d\r\n", FB_BOARD_NAME, r);
    xTaskCreate(can_task, "can", 384, 0, 2, 0);
    vTaskStartScheduler();
    for (;;) {}
}
