#pragma once
#include <stdint.h>
/* Battery node health FSM: NORMAL -> WARNING -> FAULT -> SAFE. MOCK(M3): thresholds are placeholders, not measured. */
typedef enum { FB_NORMAL, FB_WARNING, FB_FAULT, FB_SAFE } fb_state_t;
typedef struct { fb_state_t state; } fb_fault_fsm_t;
void fb_fault_init(fb_fault_fsm_t *f);
void fb_fault_step(fb_fault_fsm_t *f, int32_t pack_mv, int32_t temp_deci_c);
