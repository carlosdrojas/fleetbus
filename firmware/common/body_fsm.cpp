#include "body_fsm.h"

namespace fleetbus {
// A non-owning controller over the caller's fixed-size state. No heap or vtable;
// the C state layout stays stable for FreeRTOS tasks and Python's simulator.
class BodyController final {
    fb_body_fsm_t &state_;
public:
    explicit BodyController(fb_body_fsm_t &state) noexcept : state_(state) {}
    void step(uint32_t now_ms, bool battery_alive, fb_state_t battery_state,
              fb_body_out_t *out) noexcept {
        const bool bad = !battery_alive || battery_state == FB_ST_FAULT || battery_state == FB_ST_SAFE;
        if (bad) {
            state_.state = FB_ST_LIMP_HOME; state_.healthy = 0;
        } else if (state_.state == FB_ST_LIMP_HOME) {
            if (!state_.healthy) { state_.healthy = 1; state_.healthy_since_ms = now_ms; }
            if ((uint32_t)(now_ms - state_.healthy_since_ms) >= state_.recover_ms) state_.state = FB_ST_NORMAL;
        }
        if (state_.state == FB_ST_LIMP_HOME) { out->lights = FB_LIGHT_FAULT; out->servo_deg = 90; out->limp_home = 1; }
        else { out->lights = state_.cmd_lights; out->servo_deg = state_.cmd_servo_deg; out->limp_home = 0; }
    }
};
}

extern "C" void fb_body_init(fb_body_fsm_t *b, uint32_t recover_ms) {
    *b = {FB_ST_NORMAL, 0, 0, recover_ms, FB_LIGHT_HEAD, 90};
}

extern "C" void fb_body_step(fb_body_fsm_t *b, uint32_t now_ms, int battery_alive,
                             fb_state_t battery_state, fb_body_out_t *out) {
    fleetbus::BodyController{*b}.step(now_ms, battery_alive != 0, battery_state, out);
}
