/* Sensors and actuators on the LP-MSPM0G3507. Call sequences from TI's
 * LP-MSPM0G3507 adc12_single_conversion and timx_timer_mode_pwm_edge_sleep
 * examples (SDK 2.11). Pins in bsp_pins.h. Verified in P4 (docs/measured.md).
 * Clocks from bsp_clock.c: ULPCLK 40 MHz (ADC0 on PD0), MCLK 80 MHz (TIMA1 on PD1), both from the 40 MHz crystal. */
#include <ti/driverlib/driverlib.h>
#include "bsp.h"
#include "bsp_pins.h"
#include "fb_types.h"

#define SERVO_PERIOD_US 20000u                        /* 50 Hz */

/* ---- ADC0: 12-bit, VDDA reference, 16x hardware averaging, one software-triggered sample per call */
int bsp_adc_init(void) {
    static const DL_ADC12_ClockConfig clk = { .clockSel = DL_ADC12_CLOCK_ULPCLK, .divideRatio = DL_ADC12_CLOCK_DIVIDE_8,
                                              .freqRange = DL_ADC12_CLOCK_FREQ_RANGE_32_TO_40 };   /* 5 MHz ADC clock */
    DL_ADC12_reset(ADC0);
    DL_ADC12_enablePower(ADC0);
    delay_cycles(16);
    DL_ADC12_setClockConfig(ADC0, (DL_ADC12_ClockConfig *)&clk);
    DL_ADC12_configHwAverage(ADC0, DL_ADC12_HW_AVG_NUM_ACC_16, DL_ADC12_HW_AVG_DEN_DIV_BY_16);
    DL_ADC12_setPowerDownMode(ADC0, DL_ADC12_POWER_DOWN_MODE_MANUAL);
    /* 10 us at 5 MHz. The dividers' source impedance is at most ~5 kohm, which settles in well under 1 us; 10 us keeps
     * a two-channel read (16x averaging) near 0.5 ms, since it runs inside the 10 ms node task. */
    DL_ADC12_setSampleTime0(ADC0, 50);
    return 0;
}

int bsp_adc_read_mv(int ch) {
    uint32_t chan = ch == 1 ? ADC_CH_THERM : ADC_CH_PACK;
    DL_ADC12_disableConversions(ADC0);
    DL_ADC12_configConversionMem(ADC0, DL_ADC12_MEM_IDX_0, chan, DL_ADC12_REFERENCE_VOLTAGE_VDDA,
        DL_ADC12_SAMPLE_TIMER_SOURCE_SCOMP0, DL_ADC12_AVERAGING_MODE_ENABLED, DL_ADC12_BURN_OUT_SOURCE_DISABLED,
        DL_ADC12_TRIGGER_MODE_AUTO_NEXT, DL_ADC12_WINDOWS_COMP_MODE_DISABLED);
    DL_ADC12_clearInterruptStatus(ADC0, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
    DL_ADC12_enableConversions(ADC0);
    DL_ADC12_startConversion(ADC0);
    for (uint32_t t = 0; !DL_ADC12_getRawInterruptStatus(ADC0, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED); t++)
        if (t > 200000u) return -1;                   /* ~a few ms at 80 MHz */
    int raw = (int)DL_ADC12_getMemResult(ADC0, DL_ADC12_MEM_IDX_0);
    return raw * 3300 / 4095;                         /* assumes VDDA = 3.3 V (LaunchPad LDO) */
}

/* ---- Servo on TIMA1 CCP0 (PB4): 80 MHz / 8 / (9 + 1) = 1 MHz tick, 20000-tick period = 50 Hz.
 * Bench bug 6 (2026-11-25): prescale was 79, i.e. 80 MHz / 8 / 80 = 125 kHz, so pulses and period were 8x long
 * (AD2: 7.90-15.80 ms pulses every 158 ms). MCLK then came from the SYSPLL on SYSOSC and measured ~1.2 % fast; the PLL now runs from the crystal.
 * Edge-aligned PWM counts down: high time = period - compare (TI example: compare 500 of 2000 gives 75 % duty). */
void bsp_servo_init(void) {
    static const DL_TimerA_ClockConfig clk = { .clockSel = DL_TIMER_CLOCK_BUSCLK, .divideRatio = DL_TIMER_CLOCK_DIVIDE_8, .prescale = 9u };
    static const DL_TimerA_PWMConfig pwm = { .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN, .period = SERVO_PERIOD_US,
                                             .isTimerWithFourCC = true, .startTimer = DL_TIMER_STOP };
    DL_TimerA_reset(TIMA1);
    DL_TimerA_enablePower(TIMA1);
    delay_cycles(16);
    DL_GPIO_initPeripheralOutputFunction(PIN_SERVO_IOMUX, PIN_SERVO_FUNC);
    DL_TimerA_setClockConfig(TIMA1, (DL_TimerA_ClockConfig *)&clk);
    DL_TimerA_initPWMMode(TIMA1, (DL_TimerA_PWMConfig *)&pwm);
    DL_TimerA_setCounterControl(TIMA1, DL_TIMER_CZC_CCCTL0_ZCOND, DL_TIMER_CAC_CCCTL0_ACOND, DL_TIMER_CLC_CCCTL0_LCOND);
    DL_TimerA_setCaptureCompareOutCtl(TIMA1, DL_TIMER_CC_OCTL_INIT_VAL_LOW, DL_TIMER_CC_OCTL_INV_OUT_DISABLED,
                                      DL_TIMER_CC_OCTL_SRC_FUNCVAL, DL_TIMERA_CAPTURE_COMPARE_0_INDEX);
    DL_TimerA_setCaptCompUpdateMethod(TIMA1, DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERA_CAPTURE_COMPARE_0_INDEX);
    DL_TimerA_enableClock(TIMA1);
    DL_TimerA_setCCPDirection(TIMA1, DL_TIMER_CC0_OUTPUT);
    bsp_servo_set_deg(90);
    DL_TimerA_startCounter(TIMA1);
}
void bsp_servo_set_deg(uint8_t deg) {
    if (deg > 180) deg = 180;
    uint32_t pulse_us = 1000u + (uint32_t)deg * 1000u / 180u;           /* 1.0 .. 2.0 ms */
    DL_TimerA_setCaptureCompareValue(TIMA1, SERVO_PERIOD_US - pulse_us, DL_TIMER_CC_0_INDEX);
}

void bsp_lights_set(uint8_t m) {
    if (m & FB_LIGHT_HEAD) DL_GPIO_setPins(PIN_LIGHTS_PORT, PIN_HEAD); else DL_GPIO_clearPins(PIN_LIGHTS_PORT, PIN_HEAD);
    if (m & FB_LIGHT_TURN) DL_GPIO_setPins(PIN_LIGHTS_PORT, PIN_TURN); else DL_GPIO_clearPins(PIN_LIGHTS_PORT, PIN_TURN);
    bsp_led((m & FB_LIGHT_FAULT) != 0);
    if (m & FB_LIGHT_FAULT) DL_GPIO_setPins(PIN_PROBE_PORT, PIN_PROBE); else DL_GPIO_clearPins(PIN_PROBE_PORT, PIN_PROBE);   /* J4.35 test point */
}
