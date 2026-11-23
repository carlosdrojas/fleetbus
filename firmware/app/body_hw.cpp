// Body node hardware: head/turn lights on GPIO, fault light = the LaunchPad LED, servo on a timer PWM (powered from
// its own 5 V supply, grounds joined). Pins: bsp/mspm0g3507/bsp_pins.h.
#include "app.hpp"
#include "bsp.h"

namespace fleetbus::app {

void hw_init() { bsp_servo_init(); }

void apply(const BodyOutputs& out) {
    bsp_lights_set(out.lights);
    bsp_servo_set_deg(out.servo_deg);
}

}  // namespace fleetbus::app
