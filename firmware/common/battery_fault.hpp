#pragma once
// Battery health state machine: NORMAL -> WARNING -> FAULT -> SAFE.
//   * every breach must persist for `debounce` consecutive samples (one noisy ADC read can't trip it)
//   * WARNING clears only after `debounce` samples inside the hysteresis band (no chatter at the threshold)
//   * FAULT latches until reset(); if the cause persists `safe_after` more samples it escalates to SAFE
// Limits are bench TARGETS, not calibrated against a real cell.
#include <cstdint>
#include "fb_types.h"

namespace fleetbus {

struct BatterySample {
    int32_t pack_mv = 0;
    int32_t ma = 0;          // signed: negative = charging
    int32_t temp_dc = 0;     // 0.1 degC
    bool sensor_ok = false;
};

class BatteryFaultFsm {
public:
    struct Config {
        int32_t warn_temp_dc = 450, fault_temp_dc = 600;   // 45.0 / 60.0 degC
        int32_t warn_mv_low = 3300, fault_mv_low = 3000;
        int32_t fault_ma_abs = 5000;
        int32_t hysteresis_dc = 20, hysteresis_mv = 50;
        uint8_t debounce = 3;                              // samples (3 x 100 ms)
        uint8_t safe_after = 10;                           // samples in FAULT before SAFE
    };

    constexpr BatteryFaultFsm() : BatteryFaultFsm(Config{}) {}
    explicit constexpr BatteryFaultFsm(const Config& cfg) : cfg_(cfg) {}

    void step(const BatterySample& s);
    void reset();

    fb_state_t state() const { return state_; }
    fb_fault_code_t code() const { return code_; }
    bool latched() const { return state_ == FB_ST_FAULT || state_ == FB_ST_SAFE; }
    const Config& config() const { return cfg_; }

private:
    fb_fault_code_t classify(const BatterySample& s) const;

    Config cfg_;
    fb_state_t state_ = FB_ST_NORMAL;
    fb_fault_code_t code_ = FB_FAULT_NONE;
    uint8_t cnt_warn_ = 0, cnt_fault_ = 0, cnt_clear_ = 0, cnt_in_fault_ = 0;
};

}  // namespace fleetbus
