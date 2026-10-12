#include <string.h>
#include "obd_poller.h"
#include "obd_allowlist.h"
#include "can_msgs.h"

static const uint8_t pids[] = {0x0D /* speed */, 0x0C /* rpm */, 0x05 /* coolant */, 0x42 /* control module voltage */};

void fb_obd_init(fb_obd_t *o, uint32_t now_ms) { memset(o, 0, sizeof *o); o->last_poll_ms = now_ms; o->parked_since_ms = now_ms; o->last_rx_ms = now_ms; }

int fb_obd_poll_tick(fb_obd_t *o, uint32_t now_ms, fb_can_frame_t *req) {
    if ((uint32_t)(now_ms - o->last_poll_ms) < FB_OBD_POLL_PERIOD_MS) return 0;
    o->last_poll_ms = now_ms;
    uint8_t pid = pids[o->next_pid_idx];
    o->next_pid_idx = (uint8_t)((o->next_pid_idx + 1u) % (sizeof pids));
    uint8_t d[8];
    if (fb_obd_build_request(0x01, pid, d) != 0) return 0;      /* cannot happen for the list above; fail closed anyway */
    req->id = FB_OBD_REQ_ID; req->dlc = 8; memcpy(req->data, d, 8);
    return 1;
}

static void update_drive(fb_obd_t *o, uint32_t now_ms) {
    fb_drive_state_t prev = o->drive;
    if ((uint32_t)(now_ms - o->last_rx_ms) > FB_OBD_PARKED_AFTER_MS) o->drive = FB_DRIVE_PARKED;
    else if (o->have_speed && o->speed_kph > 0) o->drive = FB_DRIVE_DRIVING;
    else if ((o->have_rpm && o->rpm_quarter > 0) || (o->have_volt && o->ctrl_mv >= FB_OBD_ENGINE_ON_MV)) o->drive = FB_DRIVE_IDLE;
    else o->drive = FB_DRIVE_PARKED;
    if (o->drive == FB_DRIVE_PARKED && prev != FB_DRIVE_PARKED) o->parked_since_ms = now_ms;
}

void fb_obd_on_frame(fb_obd_t *o, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (id < 0x7E8 || id > 0x7EF || dlc < 4) { update_drive(o, now_ms); return; }
    /* ISO-TP single frame: data[0] = payload length, then service+0x40, PID, A, B... */
    uint8_t len = data[0];
    if (len < 3 || len > 7 || data[1] != 0x41) { o->bad_replies++; update_drive(o, now_ms); return; }
    uint8_t pid = data[2], a = dlc > 3 ? data[3] : 0, b = dlc > 4 ? data[4] : 0;
    o->rx_frames++; o->last_rx_ms = now_ms;
    switch (pid) {
    case 0x0D: if (len >= 3) { o->speed_kph = a; o->have_speed = 1; } break;
    case 0x0C: if (len >= 4) { o->rpm_quarter = (uint16_t)(((uint16_t)a << 8) | b); o->have_rpm = 1; } break;   /* (256A+B)/4 rpm == 256A+B quarter-rpm */
    case 0x05: if (len >= 3) { o->coolant_raw = a; o->have_coolant = 1; } break;
    case 0x42: if (len >= 4) { o->ctrl_mv = (uint16_t)(((uint16_t)a << 8) | b); o->have_volt = 1; } break;       /* (256A+B)/1000 V */
    default: break;
    }
    update_drive(o, now_ms);
}

int fb_obd_should_sleep(const fb_obd_t *o, uint32_t now_ms) {
    fb_obd_t c = *o;
    update_drive(&c, now_ms);
    return c.drive == FB_DRIVE_PARKED && (uint32_t)(now_ms - c.parked_since_ms) >= FB_OBD_SLEEP_SILENCE_MS;
}

uint8_t fb_obd_vehicle_frame(const fb_obd_t *o, uint32_t *id, uint8_t data[8]) {
    struct can_msgs_vehicle_obd_t m = {.speed_kph = o->speed_kph, .rpm_quarter = o->rpm_quarter, .coolant_c = o->coolant_raw, .proto_ver = FB_PROTO_VER};
    can_msgs_vehicle_obd_pack(data, &m, 8);
    *id = CAN_MSGS_VEHICLE_OBD_FRAME_ID;
    return 8;
}
