#pragma once
#include <stdint.h>
#include "fb_types.h"

/* Body node: NORMAL <-> LIMP_HOME. Enters limp-home when the battery node reports FAULT/SAFE or goes silent
 * (fail-safe). Leaves only after the battery node has been healthy for `recover_ms`. */
#define FB_LIGHT_HEAD 0x01
#define FB_LIGHT_TURN 0x02
#define FB_LIGHT_FAULT 0x04

typedef struct {
    fb_state_t state;
    uint32_t healthy_since_ms; int healthy;
    uint32_t recover_ms;
    uint8_t cmd_lights; uint8_t cmd_servo_deg;   /* requested from the bench / operator */
} fb_body_fsm_t;

typedef struct { uint8_t lights; uint8_t servo_deg; uint8_t limp_home; } fb_body_out_t;

void fb_body_init(fb_body_fsm_t *b, uint32_t recover_ms);
void fb_body_step(fb_body_fsm_t *b, uint32_t now_ms, int battery_alive, fb_state_t battery_state, fb_body_out_t *out);
