#include "fault_fsm.h"
void fb_fault_init(fb_fault_fsm_t *f) { f->state = FB_NORMAL; }
/* MOCK(M3): placeholder thresholds. */
void fb_fault_step(fb_fault_fsm_t *f, int32_t pack_mv, int32_t temp_deci_c) {
    if (temp_deci_c > 600 || pack_mv < 3000) f->state = (f->state == FB_FAULT || f->state == FB_SAFE) ? FB_SAFE : FB_FAULT;
    else if (temp_deci_c > 450 || pack_mv < 3300) f->state = (f->state == FB_NORMAL) ? FB_WARNING : f->state;
    else if (f->state == FB_WARNING) f->state = FB_NORMAL;
}
