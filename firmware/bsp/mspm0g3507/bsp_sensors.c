/* Sensors and actuators on the LP-MSPM0G3507. Call sequences from TI's
 * LP-MSPM0G3507 adc12_single_conversion, i2c_controller_rw_multibyte_fifo_poll and timx_timer_mode_pwm_edge_sleep
 * examples (SDK 2.11). Pins in bsp_pins.h. NOT YET RUN ON A BOARD.
 * Clock assumptions from bsp_clock.c: ULPCLK 40 MHz (ADC0, I2C1 on PD0), MCLK 80 MHz (TIMA1 on PD1). VERIFY in P4. */
#include <ti/driverlib/driverlib.h>
#include "bsp.h"
#include "bsp_pins.h"
#include "fb_types.h"


#define ULPCLK_HZ 40000000u
#define INA219_ADDR 0x40u
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
    DL_ADC12_setSampleTime0(ADC0, 500);               /* 100 us at 5 MHz: plenty for a high-impedance divider */
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

/* ---- I2C1 controller, 100 kHz: SCL = 40 MHz / ((1 + 39) * 10) */
int bsp_i2c_init(void) {
    static const DL_I2C_ClockConfig clk = { .clockSel = DL_I2C_CLOCK_BUSCLK, .divideRatio = DL_I2C_CLOCK_DIVIDE_1 };
    DL_I2C_reset(I2C1);
    DL_I2C_enablePower(I2C1);
    delay_cycles(16);
    DL_GPIO_initPeripheralInputFunctionFeatures(PIN_I2C_SDA_IOMUX, PIN_I2C_SDA_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_initPeripheralInputFunctionFeatures(PIN_I2C_SCL_IOMUX, PIN_I2C_SCL_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(PIN_I2C_SDA_IOMUX);             /* open drain; pull-ups are on the INA219 breakout */
    DL_GPIO_enableHiZ(PIN_I2C_SCL_IOMUX);
    DL_I2C_setClockConfig(I2C1, (DL_I2C_ClockConfig *)&clk);
    DL_I2C_disableAnalogGlitchFilter(I2C1);
    DL_I2C_resetControllerTransfer(I2C1);
    DL_I2C_setTimerPeriod(I2C1, ULPCLK_HZ / (10u * 100000u) - 1u);
    DL_I2C_setControllerTXFIFOThreshold(I2C1, DL_I2C_TX_FIFO_LEVEL_EMPTY);
    DL_I2C_setControllerRXFIFOThreshold(I2C1, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    DL_I2C_enableControllerClockStretching(I2C1);
    DL_I2C_enableController(I2C1);
    return 0;
}

static int wait_idle(void) {
    for (uint32_t t = 0; !(DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_IDLE); t++) if (t > 100000u) return -1;
    return 0;
}
static int wait_done(void) {
    for (uint32_t t = 0; DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_BUSY; t++) if (t > 100000u) return -1;
    return (DL_I2C_getControllerStatus(I2C1) & DL_I2C_CONTROLLER_STATUS_ERROR) ? -1 : 0;
}

/* INA219 shunt-voltage register (0x01), 10 uV/LSB, 0.1 ohm shunt => 0.1 mA per LSB. Calibration register unused. */
int bsp_ina219_read_ma(int32_t *ma) {
    uint8_t reg = 0x01, rx[2];
    if (wait_idle() != 0) goto fail;
    DL_I2C_flushControllerTXFIFO(I2C1);
    DL_I2C_fillControllerTXFIFO(I2C1, &reg, 1);
    DL_I2C_startControllerTransfer(I2C1, INA219_ADDR, DL_I2C_CONTROLLER_DIRECTION_TX, 1);
    if (wait_done() != 0 || wait_idle() != 0) goto fail;
    DL_I2C_startControllerTransfer(I2C1, INA219_ADDR, DL_I2C_CONTROLLER_DIRECTION_RX, 2);
    for (int i = 0; i < 2; i++) {
        for (uint32_t t = 0; DL_I2C_isControllerRXFIFOEmpty(I2C1); t++) if (t > 100000u) goto fail;
        rx[i] = DL_I2C_receiveControllerData(I2C1);
    }
    if (wait_done() != 0) goto fail;
    *ma = (int16_t)((rx[0] << 8) | rx[1]) / 10;
    return 0;
fail:
    DL_I2C_resetControllerTransfer(I2C1);             /* e.g. NACK because the breakout is unplugged */
    return -1;
}

/* ---- Servo on TIMA1 CCP0 (PB4): 80 MHz / 8 / (79 + 1) = 1 MHz tick, 20000-tick period = 50 Hz.
 * Edge-aligned PWM counts down: high time = period - compare (TI example: compare 500 of 2000 gives 75 % duty). */
void bsp_servo_init(void) {
    static const DL_TimerA_ClockConfig clk = { .clockSel = DL_TIMER_CLOCK_BUSCLK, .divideRatio = DL_TIMER_CLOCK_DIVIDE_8, .prescale = 79u };
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
}
