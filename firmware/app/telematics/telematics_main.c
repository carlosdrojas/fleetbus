/* In-car telematics node (phase 4b). UNVERIFIED ON HARDWARE; compiles and links, never run on a board or in the car.
 *
 *   Sentra OBD-II CAN  <-- listens, plus allowlisted mode-01 polls through car_tx() -->  this node  <-- UART link -->  Pi gateway
 *
 * Rules (AGENTS.md): never write to the car. The car bus is touched only by car_tx(); link frames (OTA from the gateway)
 * go to the node core and are never forwarded to the car bus. When the car is off the node stops polling (the OBD-II port
 * stays powered: unplug it when parked until real low-power sleep is implemented and measured). */
#include "FreeRTOS.h"
#include "task.h"
#include "stm32g4xx_hal.h"
#include "bsp.h"
#include "node_core.h"
#include "obd_poller.h"
#include "uart_link.h"
#include "car_tx.h"
#include "can_msgs.h"
#include "../keys/fleetbus_pub.h"

#ifndef APP_SLOT
#error "APP_SLOT must be defined"
#endif

static fb_node_t g_node;
static fb_obd_t g_obd;
static fb_link_rx_t g_rx;
static volatile uint32_t g_chk, g_tasks_ok = 1;

static void link_send(uint32_t id, const uint8_t *d, uint8_t dlc) { uint8_t enc[FB_LINK_MAX_ENCODED]; bsp_link_write(enc, fb_link_encode(id, d, dlc, enc)); }

static void telematics_task(void *arg) {
    (void)arg;
    fb_boot_meta_t m; fb_meta_load(bsp_flash(), &m);
    uint32_t ver = ((const uint32_t *)FB_SLOT_ADDR(APP_SLOT))[2];
    fb_node_init(&g_node, FB_NODE_TELEMATICS, (uint16_t)ver, ver, APP_SLOT, bsp_flash(), FB_PUBKEY, m.status[APP_SLOT] == FB_SLOT_TRIAL);
    g_node.boot_ms = bsp_millis() ? bsp_millis() : 1; g_node.state = FB_ST_NORMAL;
    fb_obd_init(&g_obd, bsp_millis());
    fb_link_rx_init(&g_rx);
    TickType_t last = xTaskGetTickCount();
    uint32_t last_vehicle = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(FB_NODE_TICK_MS));
        uint32_t now = bsp_millis();
        uint32_t id; uint8_t d[8], dlc;
        for (int i = 0; i < 32 && bsp_can_receive(&id, d, &dlc); i++) fb_obd_on_frame(&g_obd, now, id, d, dlc);   /* car bus: listen */
        uint8_t b; fb_can_frame_t lf;
        for (int i = 0; i < 64 && bsp_link_read_byte(&b); i++)                                                   /* gateway link: OTA only */
            if (fb_link_feed(&g_rx, b, &lf) && (lf.id == 0x400 || lf.id == 0x401)) fb_node_on_can(&g_node, now, lf.id, lf.data, lf.dlc);
        int asleep = fb_obd_should_sleep(&g_obd, now);
        fb_can_frame_t req;
        if (!asleep && fb_obd_poll_tick(&g_obd, now, &req)) car_tx(&req);                                       /* the only car-bus TX path */
        g_node.tasks_ok = (int)g_tasks_ok;
        fb_sensors_t none = {0, 0, 0, 1};
        fb_node_tick(&g_node, now, &none);
        fb_can_frame_t f;
        while (fb_node_pop_tx(&g_node, &f)) link_send(f.id, f.data, f.dlc);                                      /* heartbeats / OTA acks -> gateway */
        if ((uint32_t)(now - last_vehicle) >= 200) { last_vehicle = now; uint32_t vid; uint8_t vd[8]; link_send(0x500, vd, fb_obd_vehicle_frame(&g_obd, &vid, vd)); }
        g_chk++;
        if (g_node.reboot_requested) { vTaskDelay(pdMS_TO_TICKS(100)); NVIC_SystemReset(); }
    }
}

static void wdg_task(void *arg) {
    (void)arg;
    uint32_t prev = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        g_tasks_ok = g_chk != prev; prev = g_chk;
        if (g_tasks_ok && !g_node.health_deadline_missed) bsp_iwdg_feed();
    }
}

void vApplicationMallocFailedHook(void) { for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t t, char *n) { (void)t; (void)n; for (;;) {} }

int main(void) {
    HAL_Init(); bsp_clock_init(); bsp_gpio_init(); bsp_log_init();      /* LPUART1 doubles as the gateway link: no text logging here */
    bsp_can_init();
    xTaskCreate(telematics_task, "telem", 512, 0, 3, 0);
    xTaskCreate(wdg_task, "wdg", 192, 0, 4, 0);
    vTaskStartScheduler();
    for (;;) {}
}
