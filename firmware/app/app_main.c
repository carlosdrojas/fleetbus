/* FleetBus node application: FreeRTOS tasks around the portable node core (common/node_core.c).
 * Role (battery / body) is chosen at build time. UNVERIFIED ON HARDWARE: compiles and links; never run on a board yet.
 *
 *  node_task  prio 3  10 ms: drain CAN RX -> node core -> drain node TX -> CAN; applies actuators
 *  sense_task prio 2  20 ms: reads sensors (battery role)
 *  wdg_task   prio 4  100 ms: feeds the watchdog only if every task checked in AND the trial-boot health deadline holds
 */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"
#include "node_core.h"
#include "pubkey_ref.h"
#include "app_roles.h"

#ifndef APP_SLOT
#error "APP_SLOT (0 or 1) must be defined by the build"
#endif

/* The node task verifies a received image (SHA-512 + Ed25519) before it marks it NEW. Measured peak stack of the
 * Ed25519 check alone (emulated, firmware/spike/p0): 1,664 B on Cortex-M4, 1,888 B on Cortex-M0+; fb_image_verify
 * adds ~0.7 KB of buffers. The old 384-word (1,536 B) stack would overflow on the first completed OTA on either
 * board. 1024 words = 4 KB; the status line prints the measured high-water mark ("stk_free") for the bench. */
#define NODE_STACK_WORDS 1024

static fb_node_t g_node;
static volatile fb_sensors_t g_sens = { 3800, 0, 250, 1 };
static volatile uint32_t g_chk_node, g_chk_sense;
static volatile int g_tasks_ok = 1;

static uint32_t slot_image_version(void) {
    const uint32_t *hdr = (const uint32_t *)FB_SLOT_ADDR(APP_SLOT);
    return hdr[2];                                  /* version at header offset 0x08 */
}

static void node_task(void *arg) {
    (void)arg;
    fb_boot_meta_t m; fb_meta_load(bsp_flash(), &m);
    int trial = m.status[APP_SLOT] == FB_SLOT_TRIAL;
    uint32_t ver = slot_image_version();
    fb_node_init(&g_node, APP_NODE_ID, (uint16_t)ver, ver, APP_SLOT, bsp_flash(), FB_PUBKEY_REF, trial);
    g_node.boot_ms = bsp_millis() ? bsp_millis() : 1; g_node.state = FB_ST_NORMAL;
    bsp_log("node %d fw v%lu slot %c %s\r\n", APP_NODE_ID, (unsigned long)ver, (int)(0x41 + APP_SLOT), trial ? "TRIAL" : "confirmed");
    TickType_t last = xTaskGetTickCount();
    uint32_t last_log = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(FB_NODE_TICK_MS));
        uint32_t now = bsp_millis();
        uint32_t id; uint8_t d[8], dlc;
        int budget = 32;
        while (budget-- > 0 && bsp_can_receive(&id, d, &dlc)) fb_node_on_can(&g_node, now, id, d, dlc);
        fb_sensors_t s; taskENTER_CRITICAL(); s = *(const fb_sensors_t *)&g_sens; taskEXIT_CRITICAL();
        g_node.tasks_ok = g_tasks_ok;
        fb_node_tick(&g_node, now, &s);
        app_actuators_apply(&g_node.act);
        fb_can_frame_t f;
        while (fb_node_pop_tx(&g_node, &f)) bsp_can_send(f.id, f.data, f.dlc);
        g_chk_node++;
        if (now - last_log >= 1000) {
            last_log = now;
            bsp_log("t=%lu st=%d peer=%d can_err=%lu tx_drop=%lu stk_free=%lu\r\n", (unsigned long)now, g_node.state,
                    fb_hb_peer_alive(&g_node.peer, now), (unsigned long)bsp_can_error_count(), (unsigned long)g_node.tx_dropped,
                    (unsigned long)uxTaskGetStackHighWaterMark(NULL) * 4u);
        }
        if (g_node.reboot_requested) { vTaskDelay(pdMS_TO_TICKS(100)); bsp_system_reset(); }   /* let the DONE ack leave the TX FIFO */
    }
}

static void sense_task(void *arg) {
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
        fb_sensors_t s; app_sensors_read(&s);
        taskENTER_CRITICAL(); *(fb_sensors_t *)&g_sens = s; taskEXIT_CRITICAL();
        g_chk_sense++;
    }
}

static void wdg_task(void *arg) {
    (void)arg;
    uint32_t pn = 0, ps = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int ok = g_chk_node != pn;
#if APP_HAS_SENSE_TASK
        ok = ok && g_chk_sense != ps;
#endif
        pn = g_chk_node; ps = g_chk_sense;
#ifdef FB_BENCH_NEVER_HEALTHY
        ok = 0;                                     /* bench-only broken image: runs, never reports healthy */
#endif
        g_tasks_ok = ok;
        if (ok && !g_node.health_deadline_missed) bsp_wdt_feed();
    }
}

void vApplicationMallocFailedHook(void) { bsp_log("malloc failed\r\n"); for (;;) {} }              /* the watchdog resets us */
void vApplicationStackOverflowHook(TaskHandle_t t, char *name) { (void)t; bsp_log("stack overflow %s\r\n", name); for (;;) {} }

int main(void) {
    bsp_board_init();
    bsp_log_init();
#ifdef FB_BENCH_HANG_AT_BOOT
    for (;;) {}                                     /* bench-only broken image: hangs; bootloader must roll back */
#endif
    app_hw_init();
    if (bsp_can_init() != 0) bsp_log("CAN init failed\r\n");
    xTaskCreate(node_task, "node", NODE_STACK_WORDS, 0, 3, 0);
#if APP_HAS_SENSE_TASK
    xTaskCreate(sense_task, "sense", 256, 0, 2, 0);
#endif
    xTaskCreate(wdg_task, "wdg", 192, 0, 4, 0);
    vTaskStartScheduler();
    for (;;) {}
}
