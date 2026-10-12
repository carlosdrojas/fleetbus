#pragma once
#include <stdint.h>
#include "fb_types.h"

/* Battery node health FSM: NORMAL -> WARNING -> FAULT -> SAFE.
 * Limits are TARGET values, not calibrated against real hardware. Each breach must persist for
 * `debounce` consecutive samples. FAULT and SAFE latch until fb_fault_reset(). */
typedef struct {
    int32_t warn_temp_dc, fault_temp_dc;     /* 0.1 C */
    int32_t warn_mv_low, fault_mv_low;       /* mV */
    int32_t fault_ma_abs;                    /* |mA| */
    int32_t hysteresis_dc, hysteresis_mv;
    uint8_t debounce;                        /* samples */
    uint8_t safe_after;                      /* samples spent in FAULT before SAFE */
} fb_fault_cfg_t;

typedef struct {
    fb_fault_cfg_t cfg;
    fb_state_t state;
    fb_fault_code_t code;
    uint8_t cnt_warn, cnt_fault, cnt_clear, cnt_in_fault;
} fb_fault_fsm_t;

void fb_fault_default_cfg(fb_fault_cfg_t *c);
void fb_fault_init(fb_fault_fsm_t *f, const fb_fault_cfg_t *cfg);
void fb_fault_step(fb_fault_fsm_t *f, int32_t pack_mv, int32_t ma, int32_t temp_dc, int sensor_ok);
void fb_fault_reset(fb_fault_fsm_t *f);
