/* Battery node hardware: INA219 current, thermistor on ADC1_IN1, potentiometer on ADC1_IN2 standing in for pack voltage
 * (turn it down to trigger undervoltage on the bench). */
#include "bsp.h"
#include "app_roles.h"

static const struct { int16_t ratio_milli; int16_t dc; } ntc[] = {
#include "ntc_lut.inc"
};

static int32_t ntc_to_dc(int ratio_milli) {          /* ratio falls as temperature rises; linear interpolation, ESTIMATED curve */
    int n = (int)(sizeof ntc / sizeof ntc[0]);
    if (ratio_milli >= ntc[0].ratio_milli) return ntc[0].dc;
    if (ratio_milli <= ntc[n - 1].ratio_milli) return ntc[n - 1].dc;
    for (int i = 0; i < n - 1; i++)
        if (ratio_milli <= ntc[i].ratio_milli && ratio_milli > ntc[i + 1].ratio_milli)
            return ntc[i].dc + (ntc[i + 1].dc - ntc[i].dc) * (ntc[i].ratio_milli - ratio_milli) / (ntc[i].ratio_milli - ntc[i + 1].ratio_milli);
    return ntc[0].dc;
}

void app_hw_init(void) { bsp_adc_init(); bsp_i2c_init(); }

void app_sensors_read(fb_sensors_t *s) {
    int t_mv = bsp_adc_read_mv(1), p_mv = bsp_adc_read_mv(2);
    int32_t ma = 0;
    int ina_ok = bsp_ina219_read_ma(&ma) == 0;
    s->sensor_ok = t_mv >= 0 && p_mv >= 0 && ina_ok;
    s->temp_dc = t_mv >= 0 ? ntc_to_dc(t_mv * 1000 / 3300) : 0;
    s->pack_mv = p_mv >= 0 ? 3000 + p_mv * 1200 / 3300 : 0;     /* pot 0..3.3 V -> fake pack 3.0..4.2 V */
    s->ma = ma;
}
void app_actuators_apply(const fb_actuators_t *a) { (void)a; }
