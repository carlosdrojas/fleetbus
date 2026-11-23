#pragma once
// Role hooks between the FreeRTOS application (app_main.cpp) and the board. One role per image, chosen by the build
// (-DFB_ROLE_BATTERY or -DFB_ROLE_BODY). Each image links only its own role file: battery_hw.cpp defines
// read_sample(), body_hw.cpp defines apply(). Both are declared for both roles so app_main.cpp can name them inside
// `if constexpr` branches that are discarded (never instantiated, never linked) for the other role.
#include "node.hpp"

namespace fleetbus::app {

#if defined(FB_ROLE_BATTERY)
using Node = BatteryNode;
#elif defined(FB_ROLE_BODY)
using Node = BodyNode;
#else
#error "define FB_ROLE_BATTERY or FB_ROLE_BODY"
#endif

void hw_init();
BatterySample read_sample();              // battery: ADC (+ INA219), converted to units
void apply(const BodyOutputs& out);       // body: lights + servo

}  // namespace fleetbus::app
