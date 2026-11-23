#include "fault_fsm.h"

namespace fleetbus {
class BatteryController final {
    fb_fault_fsm_t &state_;
    static uint8_t bump(uint8_t count) noexcept {
        return count == UINT8_MAX ? count : static_cast<uint8_t>(count + 1);
    }
public:
    explicit BatteryController(fb_fault_fsm_t &state) noexcept : state_(state) {}
    void step(int32_t pack_mv, int32_t ma, int32_t temp_dc, bool sensor_ok) noexcept {
        const fb_fault_cfg_t *c = &state_.cfg;
        if (state_.state == FB_ST_SAFE) return;

        fb_fault_code_t fcode = FB_FAULT_NONE;
        if (!sensor_ok) fcode = FB_FAULT_SENSOR;
        else if (temp_dc >= c->fault_temp_dc) fcode = FB_FAULT_OVERTEMP;
        else if (pack_mv <= c->fault_mv_low) fcode = FB_FAULT_UNDERVOLT;
        else if (ma > c->fault_ma_abs || ma < -static_cast<int64_t>(c->fault_ma_abs)) fcode = FB_FAULT_OVERCURRENT;

        if (state_.state == FB_ST_FAULT) {            /* latched; escalate to SAFE if it persists */
            if (fcode != FB_FAULT_NONE) state_.cnt_in_fault = bump(state_.cnt_in_fault);
            if (state_.cnt_in_fault >= c->safe_after) state_.state = FB_ST_SAFE;
            return;
        }

        state_.cnt_fault = fcode != FB_FAULT_NONE ? bump(state_.cnt_fault) : 0;
        if (state_.cnt_fault >= c->debounce) {
            state_.state = FB_ST_FAULT; state_.code = fcode; state_.cnt_in_fault = 0; return;
        }

        const bool warn_now = temp_dc >= c->warn_temp_dc || pack_mv <= c->warn_mv_low;
        const bool clear_now = temp_dc < c->warn_temp_dc - c->hysteresis_dc && pack_mv > c->warn_mv_low + c->hysteresis_mv;

        if (state_.state == FB_ST_NORMAL) {
            state_.cnt_warn = warn_now ? bump(state_.cnt_warn) : 0;
            if (state_.cnt_warn >= c->debounce) {
                state_.state = FB_ST_WARNING; state_.cnt_clear = 0;
                state_.code = temp_dc >= c->warn_temp_dc ? FB_FAULT_OVERTEMP : FB_FAULT_UNDERVOLT;
            }
        } else if (state_.state == FB_ST_WARNING) {
            state_.cnt_clear = clear_now ? bump(state_.cnt_clear) : 0;
            if (state_.cnt_clear >= c->debounce) { state_.state = FB_ST_NORMAL; state_.code = FB_FAULT_NONE; state_.cnt_warn = 0; }
        }
    }
};
}


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


extern "C" void fb_fault_step(fb_fault_fsm_t *f, int32_t pack_mv, int32_t ma,
                              int32_t temp_dc, int sensor_ok) {
    fleetbus::BatteryController{*f}.step(pack_mv, ma, temp_dc, sensor_ok != 0);
}
