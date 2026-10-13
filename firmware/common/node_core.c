#include <string.h>
#include "node_core.h"
#include "can_msgs.h"
#include "image.h"

static void push_tx(fb_node_t *n, uint32_t id, const uint8_t *d, uint8_t dlc) {
    uint8_t next = (uint8_t)((n->tx_head + 1) % FB_TX_QUEUE);
    if (next == n->tx_tail) { n->tx_dropped++; return; }
    fb_can_frame_t *f = &n->txq[n->tx_head];
    f->id = id; f->dlc = dlc; memcpy(f->data, d, dlc);
    n->tx_head = next;
}

int fb_node_pop_tx(fb_node_t *n, fb_can_frame_t *out) {
    if (n->tx_head == n->tx_tail) return 0;
    *out = n->txq[n->tx_tail];
    n->tx_tail = (uint8_t)((n->tx_tail + 1) % FB_TX_QUEUE);
    return 1;
}

void fb_node_init(fb_node_t *n, fb_node_id_t id, uint16_t fw_version, uint32_t image_version, int running_slot,
                  const fb_flash_t *flash, const uint8_t pubkey[32], int trial_boot) {
    memset(n, 0, sizeof *n);
    n->id = id; n->fw_version = fw_version; n->image_version = image_version; n->running_slot = running_slot;
    n->flash = flash; n->pubkey = pubkey;
    n->state = FB_ST_BOOT;
    fb_hb_init(&n->peer, FB_PEER_TIMEOUT_MS);
    fb_fault_cfg_t c; fb_fault_default_cfg(&c); fb_fault_init(&n->fault, &c);
    fb_body_init(&n->body, 1000);
    fb_ota_init(&n->ota, flash);
    n->trial = trial_boot; n->confirmed = !trial_boot; n->tasks_ok = 1;
}

static void send_ack(fb_node_t *n, uint16_t next, uint8_t status) {
    struct can_msgs_ota_ack_t a = {.next_block = next, .status = status, .node_id = (uint8_t)n->id, .proto_ver = FB_PROTO_VER};
    uint8_t b[8]; can_msgs_ota_ack_pack(b, &a, 8); push_tx(n, CAN_MSGS_OTA_ACK_FRAME_ID, b, 8);
}

static void on_ota_ctrl(fb_node_t *n, const uint8_t *data, uint8_t dlc) {
    struct can_msgs_ota_ctrl_t c;
    if (can_msgs_ota_ctrl_unpack(&c, data, dlc) != 0 || c.target_node != n->id || c.proto_ver != FB_PROTO_VER) return;
    int inactive = 1 - n->running_slot;
    if (c.cmd == 1) {                                           /* BEGIN */
        if (c.slot != inactive || fb_ota_begin(&n->ota, inactive, c.total_blocks) != 0) { send_ack(n, 0, 1); return; }
        n->ota_active = 1; n->ota_slot = (uint8_t)inactive; n->ota_since_ack = 0;
        send_ack(n, 0, 0);
    } else if (c.cmd == 2) {                                    /* ABORT */
        fb_ota_abort(&n->ota); n->ota_active = 0;
    } else if (c.cmd == 3 && n->ota_active) {                   /* FINISH */
        uint32_t ver = 0;
        /* New image must be strictly newer than what is running, and at least the anti-downgrade floor. */
        fb_boot_meta_t m; fb_meta_load(n->flash, &m);
        uint32_t floor = m.min_version > n->image_version + 1 ? m.min_version : n->image_version + 1;
        int r = n->ota.state == FB_OTA_DONE ? fb_image_verify(n->flash, inactive, (uint8_t)n->id, floor, n->pubkey, &ver) : FB_IMG_ERR_SIZE;
        n->ota_active = 0;
        if (r == FB_IMG_OK && fb_meta_mark_new(n->flash, inactive, ver) == 0) { send_ack(n, n->ota.next_block, 3); n->reboot_requested = 1; }
        else send_ack(n, n->ota.next_block, 2);
    }
}

static void on_ota_data(fb_node_t *n, const uint8_t *data, uint8_t dlc) {
    struct can_msgs_ota_data_t d;
    if (!n->ota_active || can_msgs_ota_data_unpack(&d, data, dlc) != 0) return;
    uint8_t blk[6] = {d.d0, d.d1, d.d2, d.d3, d.d4, d.d5};
    uint16_t next;
    int r = fb_ota_block(&n->ota, d.block, blk, &next);
    n->ota_since_ack++;
    if (r != FB_OTA_ACK_OK) { send_ack(n, next, r == FB_OTA_ACK_FLASH ? 4 : 1); n->ota_active = r == FB_OTA_ACK_FLASH ? 0 : 1; n->ota_since_ack = 0; }
    else if (n->ota_since_ack >= 8 || n->ota.state == FB_OTA_DONE) { send_ack(n, next, 0); n->ota_since_ack = 0; }
}

void fb_node_on_can(fb_node_t *n, uint32_t now_ms, uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (id == CAN_MSGS_HEARTBEAT_FRAME_ID) {
        struct can_msgs_heartbeat_t h;
        if (can_msgs_heartbeat_unpack(&h, data, dlc) != 0 || h.proto_ver != FB_PROTO_VER) return;
        fb_node_id_t peer = n->id == FB_NODE_BATTERY ? FB_NODE_BODY : FB_NODE_BATTERY;
        if (n->id != FB_NODE_TELEMATICS && h.node_id == peer) { fb_hb_feed(&n->peer, now_ms); n->peer_state = (fb_state_t)h.state; n->peer_id_seen = 1; }
    } else if (id == CAN_MSGS_BATTERY_STATUS_FRAME_ID && n->id == FB_NODE_BODY) {
        struct can_msgs_battery_status_t b;
        if (can_msgs_battery_status_unpack(&b, data, dlc) != 0 || b.proto_ver != FB_PROTO_VER) return;
        n->peer_batt = (fb_peer_battery_t){ .pack_mv = b.pack_milli_volt, .ma = b.current_milli_amp, .temp_dc = b.temp_deci_c,
                                            .soc_pct = b.soc_pct, .at_ms = now_ms ? now_ms : 1, .valid = 1 };
    } else if (id == CAN_MSGS_OTA_CTRL_FRAME_ID) on_ota_ctrl(n, data, dlc);
    else if (id == CAN_MSGS_OTA_DATA_FRAME_ID) on_ota_data(n, data, dlc);
}

static void send_heartbeat(fb_node_t *n, uint32_t now_ms) {
    struct can_msgs_heartbeat_t h = {.node_id = (uint8_t)n->id, .fw_version = n->fw_version, .uptime_s = (now_ms - n->boot_ms) / 1000u,
                                     .state = (uint8_t)(n->ota_active ? FB_ST_UPDATING : n->state),
        .slot = (uint8_t)n->running_slot, .trial = (uint8_t)(n->trial && !n->confirmed), .proto_ver = FB_PROTO_VER};
    uint8_t b[8]; can_msgs_heartbeat_pack(b, &h, 8); push_tx(n, CAN_MSGS_HEARTBEAT_FRAME_ID, b, 8);
}

void fb_node_tick(fb_node_t *n, uint32_t now_ms, const fb_sensors_t *s) {
    if (n->boot_ms == 0 && n->state == FB_ST_BOOT) n->boot_ms = now_ms ? now_ms : 1;
    int peer_alive = fb_hb_peer_alive(&n->peer, now_ms);

    if (n->id == FB_NODE_BATTERY) {
        if ((uint32_t)(now_ms - n->last_status_ms) >= FB_STATUS_PERIOD_MS || n->last_status_ms == 0) {
            fb_state_t before = n->fault.state;
            fb_fault_step(&n->fault, s->pack_mv, s->ma, s->temp_dc, s->sensor_ok);
            n->state = n->fault.state;
            struct can_msgs_battery_status_t b = {.pack_milli_volt = (uint16_t)s->pack_mv, .current_milli_amp = (int16_t)s->ma,
                .temp_deci_c = (int16_t)s->temp_dc, .soc_pct = (uint8_t)(s->pack_mv >= 4200 ? 100 : s->pack_mv <= 3000 ? 0 : (s->pack_mv - 3000) * 100 / 1200),
                .proto_ver = FB_PROTO_VER};
            uint8_t buf[8]; can_msgs_battery_status_pack(buf, &b, 8); push_tx(n, CAN_MSGS_BATTERY_STATUS_FRAME_ID, buf, 8);
            if ((n->fault.state >= FB_ST_FAULT && before < FB_ST_FAULT) ||
                (n->fault.state >= FB_ST_FAULT && (uint32_t)(now_ms - n->last_fault_tx_ms) >= 1000u)) {
                struct can_msgs_fault_event_t fe = {.fault_code = (uint16_t)n->fault.code, .severity = (uint8_t)(n->fault.state == FB_ST_SAFE ? 3 : 2),
                    .node_id = (uint8_t)n->id, .proto_ver = FB_PROTO_VER};
                can_msgs_fault_event_pack(buf, &fe, 8); push_tx(n, CAN_MSGS_FAULT_EVENT_FRAME_ID, buf, 8);
                n->last_fault_tx_ms = now_ms;
            }
            n->last_status_ms = now_ms;
        }
    } else if (n->id == FB_NODE_BODY) {
        fb_body_out_t o;
        fb_body_step(&n->body, now_ms, peer_alive, n->peer_state, &o);
        n->state = n->body.state;
        n->act.lights = o.lights; n->act.servo_deg = o.servo_deg; n->act.limp_home = o.limp_home;
        if ((uint32_t)(now_ms - n->last_status_ms) >= FB_STATUS_PERIOD_MS || n->last_status_ms == 0) {
            struct can_msgs_body_status_t bs = {.lights = o.lights, .servo_deg = o.servo_deg, .limp_home = o.limp_home, .proto_ver = FB_PROTO_VER};
            uint8_t buf[8]; can_msgs_body_status_pack(buf, &bs, 8); push_tx(n, CAN_MSGS_BODY_STATUS_FRAME_ID, buf, 4);
            n->last_status_ms = now_ms;
        }
    } else {
        n->state = FB_ST_NORMAL;
    }

    if ((uint32_t)(now_ms - n->last_hb_ms) >= FB_HB_PERIOD_MS || n->last_hb_ms == 0) { send_heartbeat(n, now_ms); n->last_hb_ms = now_ms; }

    /* Trial-boot health gate: confirm only if every task is alive and the peer is heard, inside the window.
     * Missing the deadline makes the platform stop feeding the watchdog, which resets into the bootloader's rollback. */
    if (n->trial && !n->confirmed) {
        int need_peer = n->id != FB_NODE_TELEMATICS;
        if (n->tasks_ok && (!need_peer || peer_alive) && n->state != FB_ST_FAULT && n->state != FB_ST_SAFE) {
            if (fb_meta_confirm(n->flash, n->running_slot) == 0) { n->confirmed = 1; n->trial = 0; }
        } else if ((uint32_t)(now_ms - n->boot_ms) >= FB_CONFIRM_WINDOW_MS) {
            n->health_deadline_missed = 1;
        }
    }
}

void fb_node_gauge_inputs(const fb_node_t *n, uint32_t now_ms, gv_inputs_t *out) {
    out->have = n->peer_batt.valid;
    out->pack_mv = n->peer_batt.pack_mv; out->ma = n->peer_batt.ma; out->temp_dc = n->peer_batt.temp_dc;
    out->soc_pct = n->peer_batt.soc_pct;
    out->battery_alive = n->peer_id_seen && fb_hb_peer_alive(&n->peer, now_ms);
    out->battery_state = n->peer_state;
    out->limp_home = n->act.limp_home;
}
