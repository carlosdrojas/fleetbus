#pragma once
/* In-car telematics logic, pure C. Everything transmitted on the car's bus comes from fb_obd_poll_tick(), which can only
 * build allowlisted OBD-II mode 01 requests (obd_allowlist.c). Responses are parsed from the standard ECU reply ids. */
#include <stdint.h>
#include "node_core.h"

#define FB_OBD_POLL_PERIOD_MS 100u          /* one request per period, round-robin over the PIDs below (TARGET) */
#define FB_OBD_PARKED_AFTER_MS 5000u        /* no ECU reply for this long -> parked */
#define FB_OBD_SLEEP_SILENCE_MS 30000u      /* parked this long -> low-power sleep */
#define FB_OBD_ENGINE_ON_MV 13000u          /* control-module voltage above this = alternator running (TARGET; measure on day one) */

typedef enum { FB_DRIVE_PARKED = 0, FB_DRIVE_IDLE = 1, FB_DRIVE_DRIVING = 2 } fb_drive_state_t;

typedef struct {
    uint8_t speed_kph; uint16_t rpm_quarter; uint8_t coolant_raw /* A, deg C + 40 */; uint16_t ctrl_mv;
    uint8_t have_speed, have_rpm, have_coolant, have_volt;
    uint32_t last_rx_ms, last_poll_ms, rx_frames, bad_replies;
    uint8_t next_pid_idx;
    fb_drive_state_t drive;
    uint32_t parked_since_ms;
} fb_obd_t;

void fb_obd_init(fb_obd_t *o, uint32_t now_ms);
/* Returns 1 and fills `req` when a request is due. `req` is guaranteed to pass fb_obd_frame_allowed(). */
int  fb_obd_poll_tick(fb_obd_t *o, uint32_t now_ms, fb_can_frame_t *req);
/* Feed every frame seen on the car bus (listen path). Updates state from OBD replies (ids 0x7E8..0x7EF). */
void fb_obd_on_frame(fb_obd_t *o, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc);
/* 1 when the node should stop polling and enter low-power sleep (engine off for a while, or no ECU answering). */
int  fb_obd_should_sleep(const fb_obd_t *o, uint32_t now_ms);
/* The VehicleObd CAN frame (id 0x500) for the gateway link. Returns dlc. */
uint8_t fb_obd_vehicle_frame(const fb_obd_t *o, uint32_t *id, uint8_t data[8]);
