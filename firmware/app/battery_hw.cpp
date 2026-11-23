// Battery node hardware. Bench stand-ins for a real cell:
//   pack voltage  potentiometer on ADC channel 2, 0-3.3 V mapped to 3.0-4.2 V (turn it down to trip undervoltage)
//   temperature   10 k NTC in a divider on ADC channel 1, through an ESTIMATED lookup curve (ntc_lut.inc)
//   current       INA219 on I2C1 only with -DFB_HAS_INA219=ON; otherwise 0 mA, so overcurrent can't fire
#include <iterator>
#include "app.hpp"
#include "bsp.h"

namespace fleetbus::app {
namespace {

struct NtcPoint { int16_t ratio_milli; int16_t dc; };
constexpr NtcPoint kNtc[] = {
#include "ntc_lut.inc"
};

// Divider ratio falls as temperature rises; linear interpolation between table points, clamped at both ends.
int32_t ntc_to_dc(int32_t ratio_milli) {
    constexpr auto n = std::size(kNtc);
    if (ratio_milli >= kNtc[0].ratio_milli) return kNtc[0].dc;
    if (ratio_milli <= kNtc[n - 1].ratio_milli) return kNtc[n - 1].dc;
    for (std::size_t i = 0; i + 1 < n; i++) {
        const NtcPoint a = kNtc[i], b = kNtc[i + 1];
        if (ratio_milli <= a.ratio_milli && ratio_milli > b.ratio_milli)
            return a.dc + (b.dc - a.dc) * (a.ratio_milli - ratio_milli) / (a.ratio_milli - b.ratio_milli);
    }
    return kNtc[0].dc;
}

bool read_current_ma(int32_t& ma) {
#ifdef FB_HAS_INA219
    return bsp_ina219_read_ma(&ma) == 0;
#else
    ma = 0;
    return true;
#endif
}

}  // namespace

void hw_init() {
    bsp_adc_init();
#ifdef FB_HAS_INA219
    bsp_i2c_init();
#endif
}

BatterySample read_sample() {
    const int t_mv = bsp_adc_read_mv(1), p_mv = bsp_adc_read_mv(2);
    BatterySample s;
    const bool current_ok = read_current_ma(s.ma);
    s.sensor_ok = t_mv >= 0 && p_mv >= 0 && current_ok;
    s.temp_dc = t_mv >= 0 ? ntc_to_dc(t_mv * 1000 / 3300) : 0;
    s.pack_mv = p_mv >= 0 ? 3000 + p_mv * 1200 / 3300 : 0;
    return s;
}

}  // namespace fleetbus::app
