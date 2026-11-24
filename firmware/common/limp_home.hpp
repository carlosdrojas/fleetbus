#pragma once
// Body node: NORMAL <-> LIMP_HOME.
// Enters limp-home at once when the battery reports FAULT/SAFE or its heartbeat goes silent (silence = fault:
// a dead battery ECU can't report its own failure). Leaves only after the battery has looked healthy for
// `recover_ms` without a break, so a flapping link doesn't toggle the lights.
#include <cstdint>
#include "fb_types.h"

namespace fleetbus {

struct BodyOutputs {
    uint8_t lights = FB_LIGHT_HEAD;
    uint8_t servo_deg = 90;
    bool limp_home = false;
};

class LimpHomeFsm {
public:
    static constexpr uint8_t kLimpServoDeg = 90;          // centred: the safe position

    explicit constexpr LimpHomeFsm(uint32_t recover_ms) : recover_ms_(recover_ms) {}

    BodyOutputs step(uint32_t now_ms, bool battery_alive, fb_state_t battery_state);
    void command(uint8_t lights, uint8_t servo_deg) { cmd_lights_ = lights; cmd_servo_deg_ = servo_deg; }

    fb_state_t state() const { return state_; }

private:
    uint32_t recover_ms_;
    fb_state_t state_ = FB_ST_NORMAL;
    bool healthy_ = false;
    uint32_t healthy_since_ms_ = 0;
    uint8_t cmd_lights_ = FB_LIGHT_HEAD, cmd_servo_deg_ = 90;   // what the operator asked for in NORMAL
};

}  // namespace fleetbus
