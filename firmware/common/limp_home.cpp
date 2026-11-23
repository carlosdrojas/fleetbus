#include "limp_home.hpp"

namespace fleetbus {

BodyOutputs LimpHomeFsm::step(uint32_t now_ms, bool battery_alive, fb_state_t battery_state) noexcept {
    const bool bad = !battery_alive || battery_state == FB_ST_FAULT || battery_state == FB_ST_SAFE;
    if (bad) {
        state_ = FB_ST_LIMP_HOME;
        healthy_ = false;                                  // any relapse restarts the recovery hold
    } else if (state_ == FB_ST_LIMP_HOME) {
        if (!healthy_) { healthy_ = true; healthy_since_ms_ = now_ms; }
        if (static_cast<uint32_t>(now_ms - healthy_since_ms_) >= recover_ms_) state_ = FB_ST_NORMAL;
    }

    BodyOutputs out;
    if (state_ == FB_ST_LIMP_HOME) {
        out.lights = FB_LIGHT_FAULT; out.servo_deg = kLimpServoDeg; out.limp_home = true;
    } else {
        out.lights = cmd_lights_; out.servo_deg = cmd_servo_deg_; out.limp_home = false;
    }
    return out;
}

}  // namespace fleetbus
