#include "body_fsm.h"

void fb_body_init(fb_body_fsm_t *b, uint32_t recover_ms) {
    b->state = FB_ST_NORMAL; b->healthy = 0; b->healthy_since_ms = 0; b->recover_ms = recover_ms;
    b->cmd_lights = FB_LIGHT_HEAD; b->cmd_servo_deg = 90;
}

void fb_body_step(fb_body_fsm_t *b, uint32_t now_ms, int battery_alive, fb_state_t battery_state, fb_body_out_t *out) {
    int bad = !battery_alive || battery_state == FB_ST_FAULT || battery_state == FB_ST_SAFE;
    if (bad) {
        b->state = FB_ST_LIMP_HOME; b->healthy = 0;
    } else if (b->state == FB_ST_LIMP_HOME) {
        if (!b->healthy) { b->healthy = 1; b->healthy_since_ms = now_ms; }
        if ((uint32_t)(now_ms - b->healthy_since_ms) >= b->recover_ms) b->state = FB_ST_NORMAL;
    }
    if (b->state == FB_ST_LIMP_HOME) { out->lights = FB_LIGHT_FAULT; out->servo_deg = 90; out->limp_home = 1; }
    else { out->lights = b->cmd_lights; out->servo_deg = b->cmd_servo_deg; out->limp_home = 0; }
}
