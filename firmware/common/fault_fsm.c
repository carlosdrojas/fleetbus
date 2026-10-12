#include "fault_fsm.h"

void fb_fault_default_cfg(fb_fault_cfg_t *c) {
    c->warn_temp_dc = 450; c->fault_temp_dc = 600;
    c->warn_mv_low = 3300; c->fault_mv_low = 3000;
    c->fault_ma_abs = 5000;
    c->hysteresis_dc = 20; c->hysteresis_mv = 50;
    c->debounce = 3; c->safe_after = 10;
}

void fb_fault_init(fb_fault_fsm_t *f, const fb_fault_cfg_t *cfg) {
    f->cfg = *cfg;
    fb_fault_reset(f);
}

void fb_fault_reset(fb_fault_fsm_t *f) {
    f->state = FB_ST_NORMAL; f->code = FB_FAULT_NONE;
    f->cnt_warn = f->cnt_fault = f->cnt_clear = f->cnt_in_fault = 0;
}

static uint8_t bump(uint8_t c) { return c == 255 ? c : (uint8_t)(c + 1); }

void fb_fault_step(fb_fault_fsm_t *f, int32_t pack_mv, int32_t ma, int32_t temp_dc, int sensor_ok) {
    const fb_fault_cfg_t *c = &f->cfg;
    if (f->state == FB_ST_SAFE) return;

    fb_fault_code_t fcode = FB_FAULT_NONE;
    if (!sensor_ok) fcode = FB_FAULT_SENSOR;
    else if (temp_dc >= c->fault_temp_dc) fcode = FB_FAULT_OVERTEMP;
    else if (pack_mv <= c->fault_mv_low) fcode = FB_FAULT_UNDERVOLT;
    else if (ma > c->fault_ma_abs || -ma > c->fault_ma_abs) fcode = FB_FAULT_OVERCURRENT;

    if (f->state == FB_ST_FAULT) {            /* latched; escalate to SAFE if it persists */
        if (fcode != FB_FAULT_NONE) f->cnt_in_fault = bump(f->cnt_in_fault);
        if (f->cnt_in_fault >= c->safe_after) f->state = FB_ST_SAFE;
        return;
    }

    f->cnt_fault = fcode != FB_FAULT_NONE ? bump(f->cnt_fault) : 0;
    if (f->cnt_fault >= c->debounce) {
        f->state = FB_ST_FAULT; f->code = fcode; f->cnt_in_fault = 0; return;
    }

    int warn_now = temp_dc >= c->warn_temp_dc || pack_mv <= c->warn_mv_low;
    int clear_now = temp_dc < c->warn_temp_dc - c->hysteresis_dc && pack_mv > c->warn_mv_low + c->hysteresis_mv;

    if (f->state == FB_ST_NORMAL) {
        f->cnt_warn = warn_now ? bump(f->cnt_warn) : 0;
        if (f->cnt_warn >= c->debounce) {
            f->state = FB_ST_WARNING; f->cnt_clear = 0;
            f->code = temp_dc >= c->warn_temp_dc ? FB_FAULT_OVERTEMP : FB_FAULT_UNDERVOLT;
        }
    } else if (f->state == FB_ST_WARNING) {
        f->cnt_clear = clear_now ? bump(f->cnt_clear) : 0;
        if (f->cnt_clear >= c->debounce) { f->state = FB_ST_NORMAL; f->code = FB_FAULT_NONE; f->cnt_warn = 0; }
    }
}
