/* Sensor and actuator self-test (P4). Bench only. Every 500 ms prints both ADC channels (mV) and the INA219 current,
 * steps the servo through 0 / 45 / 90 / 135 / 180 degrees and cycles the lights (head, turn, fault LED).
 * Checks: turn the pot and watch ch2 move 0..3300 mV; warm the thermistor and watch ch1 move; put a known load
 * through the shunt and compare mA with a multimeter; scope the servo pin for 1.0..2.0 ms pulses at 20 ms.
 * Board-neutral (bsp.h only). NOT YET RUN ON A BOARD. */
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"
#include "fb_types.h"
#include "body_fsm.h"     /* FB_LIGHT_* */

static void io_task(void *arg) {
    (void)arg;
    static const uint8_t lights[] = { 0, FB_LIGHT_HEAD, FB_LIGHT_TURN, FB_LIGHT_FAULT, FB_LIGHT_HEAD | FB_LIGHT_TURN };
    for (uint32_t n = 0;; n++) {
        int32_t ma = 0;
        int ina = bsp_ina219_read_ma(&ma);
        uint8_t deg = (uint8_t)((n % 5) * 45);
        bsp_servo_set_deg(deg);
        bsp_lights_set(lights[n % 5]);
        bsp_log("adc ch1=%d mV ch2=%d mV ina219=%s%ld mA servo=%u deg lights=%x\r\n", bsp_adc_read_mv(1),
                bsp_adc_read_mv(2), ina == 0 ? "" : "NO-ACK ", (long)ma, (unsigned)deg, (unsigned)lights[n % 5]);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void vApplicationMallocFailedHook(void) { for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t t, char *name) { (void)t; bsp_log("stack overflow %s\r\n", name); for (;;) {} }

int main(void) {
    bsp_board_init();
    bsp_log_init();
    bsp_log("\r\nFleetBus IO self-test on %s: adc %d i2c %d\r\n", FB_BOARD_NAME, bsp_adc_init(), bsp_i2c_init());
    bsp_servo_init();
    xTaskCreate(io_task, "io", 384, 0, 2, 0);
    vTaskStartScheduler();
    for (;;) {}
}
