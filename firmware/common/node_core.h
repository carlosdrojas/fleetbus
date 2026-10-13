#pragma once
/* Portable node application core. No hardware, no RTOS: the FreeRTOS tasks on the board and the host simulator
 * (firmware/host/fbsim.c, used by the gateway --sim and HIL tests) drive exactly this code.
 *
 *   fb_node_on_can()  every received CAN frame
 *   fb_node_tick()    every FB_NODE_TICK_MS with fresh sensor readings
 *   fb_node_pop_tx()  drain frames the node wants to transmit
 */
#include <stdint.h>
#include "fb_types.h"
#include "flash_if.h"
#include "fault_fsm.h"
#include "body_fsm.h"
#include "hb_monitor.h"
#include "ota_receiver.h"
#include "boot_meta.h"
#include "gauge_view.h"

#define FB_NODE_TICK_MS      10u
#define FB_HB_PERIOD_MS      100u
#define FB_STATUS_PERIOD_MS  100u
#define FB_PEER_TIMEOUT_MS   500u     /* TARGET: 5 missed heartbeats */
#define FB_CONFIRM_WINDOW_MS 10000u   /* trial boot must prove health within this window */
#define FB_TX_QUEUE          16u

typedef struct { uint32_t id; uint8_t dlc; uint8_t data[8]; } fb_can_frame_t;

typedef struct { int32_t pack_mv, ma, temp_dc; int sensor_ok; } fb_sensors_t;   /* battery node inputs */
typedef struct { uint8_t lights, servo_deg, limp_home; } fb_actuators_t;         /* body node outputs */
/* Latest BatteryStatus seen by the body node (for its gauge display, FleetBus Lite option 4). */
typedef struct { int32_t pack_mv, ma, temp_dc; uint8_t soc_pct; uint32_t at_ms; int valid; } fb_peer_battery_t;

typedef struct {
    fb_node_id_t id;
    uint16_t fw_version;
    uint32_t image_version;       /* version in the running slot's header */
    int running_slot;
    const fb_flash_t *flash;
    const uint8_t *pubkey;

    fb_state_t state;
    uint32_t boot_ms, last_hb_ms, last_status_ms, last_fault_tx_ms;
    fb_hb_monitor_t peer;
    fb_state_t peer_state;
    fb_peer_battery_t peer_batt;  /* body: last BatteryStatus received */
    int peer_id_seen;

    fb_fault_fsm_t fault;         /* battery */
    fb_body_fsm_t body;           /* body */
    fb_actuators_t act;

    fb_ota_rx_t ota;
    int ota_active;
    uint8_t ota_slot;
    uint16_t ota_since_ack;
    int reboot_requested;

    int tasks_ok;                 /* set by the platform watchdog task: all RTOS tasks checked in */
    int confirmed;                /* running slot is CONFIRMED (or no bootloader involved) */
    int trial;                    /* running slot is in TRIAL and awaiting confirm */
    int health_deadline_missed;   /* platform must stop feeding the watchdog when this is set */

    fb_can_frame_t txq[FB_TX_QUEUE];
    uint8_t tx_head, tx_tail;
    uint32_t tx_dropped;
} fb_node_t;

void fb_node_init(fb_node_t *n, fb_node_id_t id, uint16_t fw_version, uint32_t image_version, int running_slot,
                  const fb_flash_t *flash, const uint8_t pubkey[32], int trial_boot);
void fb_node_on_can(fb_node_t *n, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc);
void fb_node_tick(fb_node_t *n, uint32_t now_ms, const fb_sensors_t *s);
int  fb_node_pop_tx(fb_node_t *n, fb_can_frame_t *out);
/* Body node: what its instrument cluster shows right now (Lite option 4). */
void fb_node_gauge_inputs(const fb_node_t *n, uint32_t now_ms, gv_inputs_t *out);
