#include "battery_fault.hpp"

namespace fleetbus {
namespace {
// Saturating increment: a counter held at its limit for hours must not wrap back to 0 and "un-debounce".
constexpr uint8_t bump(uint8_t n) noexcept { return n == UINT8_MAX ? n : static_cast<uint8_t>(n + 1); }
}  // namespace

fb_fault_code_t BatteryFaultFsm::classify(const BatterySample& s) const noexcept {
    if (!s.sensor_ok) return FB_FAULT_SENSOR;              // can't trust any reading: fail safe
    if (s.temp_dc >= cfg_.fault_temp_dc) return FB_FAULT_OVERTEMP;
    if (s.pack_mv <= cfg_.fault_mv_low) return FB_FAULT_UNDERVOLT;
    // Widen before negating: -INT32_MIN overflows int32_t.
    const int64_t ma = s.ma;
    if (ma > cfg_.fault_ma_abs || ma < -static_cast<int64_t>(cfg_.fault_ma_abs)) return FB_FAULT_OVERCURRENT;
    return FB_FAULT_NONE;
}

void BatteryFaultFsm::step(const BatterySample& s) noexcept {
    if (state_ == FB_ST_SAFE) return;                      // terminal until reset()

    const fb_fault_code_t fcode = classify(s);

    if (state_ == FB_ST_FAULT) {                           // latched; escalate if the cause persists
        if (fcode != FB_FAULT_NONE) cnt_in_fault_ = bump(cnt_in_fault_);
        if (cnt_in_fault_ >= cfg_.safe_after) state_ = FB_ST_SAFE;
        return;
    }

    cnt_fault_ = fcode != FB_FAULT_NONE ? bump(cnt_fault_) : 0;
    if (cnt_fault_ >= cfg_.debounce) {
        state_ = FB_ST_FAULT; code_ = fcode; cnt_in_fault_ = 0;
        return;
    }

    const bool warn_now = s.temp_dc >= cfg_.warn_temp_dc || s.pack_mv <= cfg_.warn_mv_low;
    const bool clear_now = s.temp_dc < cfg_.warn_temp_dc - cfg_.hysteresis_dc &&
                           s.pack_mv > cfg_.warn_mv_low + cfg_.hysteresis_mv;

    if (state_ == FB_ST_NORMAL) {
        cnt_warn_ = warn_now ? bump(cnt_warn_) : 0;
        if (cnt_warn_ >= cfg_.debounce) {
            state_ = FB_ST_WARNING; cnt_clear_ = 0;
            code_ = s.temp_dc >= cfg_.warn_temp_dc ? FB_FAULT_OVERTEMP : FB_FAULT_UNDERVOLT;
        }
    } else if (state_ == FB_ST_WARNING) {
        cnt_clear_ = clear_now ? bump(cnt_clear_) : 0;
        if (cnt_clear_ >= cfg_.debounce) { state_ = FB_ST_NORMAL; code_ = FB_FAULT_NONE; cnt_warn_ = 0; }
    }
}

void BatteryFaultFsm::reset() noexcept {
    state_ = FB_ST_NORMAL; code_ = FB_FAULT_NONE;
    cnt_warn_ = cnt_fault_ = cnt_clear_ = cnt_in_fault_ = 0;
}

}  // namespace fleetbus
