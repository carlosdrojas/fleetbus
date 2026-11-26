/* MSPM0G3507 clocks, GPIO, delay, reset and watchdog.
 * Clock tree (from TI's LP-MSPM0G3507 sysctl_mclk_syspll, mcan and secondary_bsl_can examples, SDK 2.11; verified on both boards):
 *   HFXT 40 MHz crystal (PA5/PA6) -> HFCLK -> SYSPLL -> MCLK/CPUCLK 80 MHz (2 flash wait states), ULPCLK 40 MHz
 *   HFCLK also clocks MCAN directly. PLL settings from TI's LP-MSPM0G3507 secondary_bsl_can example (SDK 2.11).
 *   Until 2026-11-25 the PLL ran from SYSOSC (internal RC); board B's MCLK measured +1.2 % (80.95 MHz) that way, so
 *   the FreeRTOS tick, UART and timers were all 1.2 % fast. CAN was always exact (crystal).
 *   LFOSC 32 kHz -> WWDT0 */
#include <ti/driverlib/driverlib.h>
#include "bsp.h"
#include "bsp_pins.h"

#define POWER_STARTUP_DELAY 16u
#define CPU_HZ 80000000u

/* SYSPLL: 40 MHz HFXT / 1 * 4 = 160 MHz VCO; CLK2X -> MCLK 80 MHz; CLK1 40 MHz (only used for the lock check). */
static const DL_SYSCTL_SYSPLLConfig k_pll = {
    .inputFreq = DL_SYSCTL_SYSPLL_INPUT_FREQ_32_48_MHZ,
    .rDivClk2x = 3, .rDivClk1 = 1, .rDivClk0 = 0,
    .enableCLK2x = DL_SYSCTL_SYSPLL_CLK2X_ENABLE, .enableCLK1 = DL_SYSCTL_SYSPLL_CLK1_ENABLE,
    .enableCLK0 = DL_SYSCTL_SYSPLL_CLK0_DISABLE,
    .sysPLLMCLK = DL_SYSCTL_SYSPLL_MCLK_CLK2X, .sysPLLRef = DL_SYSCTL_SYSPLL_REF_HFCLK,
    .qDiv = 3, .pDiv = DL_SYSCTL_SYSPLL_PDIV_1,
};

/* Errata SYSPLL_ERR_01: the PLL can report "good" while locked to the wrong frequency. Measure SYSPLLCLK1 and its own
 * reference (HFCLK, the crystal) against LFCLK with the frequency clock counter; both are 40 MHz, so the ratio must be
 * 1.000 within 0.3 % (TI's workaround, reference changed from SYSOSC). Comparing against SYSOSC no longer works: it is
 * an RC oscillator ~1 % off, and would never match a crystal-locked PLL. */
static uint32_t fcc_count(DL_SYSCTL_FCC_CLOCK_SOURCE src) {
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE, DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK, src);
    DL_SYSCTL_startFCC();
    for (int t = 0; t < 66 && !DL_SYSCTL_isFCCDone(); t++) delay_cycles(977);   /* ~2 ms timeout */
    return DL_SYSCTL_readFCC();
}
static int pll_locked_right(void) {
    DL_SYSCTL_setFCCPeriods(DL_SYSCTL_FCC_TRIG_CNT_01);
    uint32_t pll = fcc_count(DL_SYSCTL_FCC_CLOCK_SOURCE_SYSPLLCLK1);
    uint32_t ref = fcc_count(DL_SYSCTL_FCC_CLOCK_SOURCE_HFCLK);
    if (ref == 0) return 0;
    uint32_t ratio = pll * 1000u / ref;              /* expected 1000 */
    return ratio > 997u && ratio < 1003u;
}

/* Back to the reset clock: MCLK on SYSOSC, SYSPLL and HFXT off, before reconfiguring (disabling the SYSPLL while it
 * is the MCLK source would stop the CPU clock). Makes a debugger "restart" without a reset safe too. */
static void bsp_clock_to_reset_state(void) {
    if (DL_SYSCTL_getMCLKSource() == DL_SYSCTL_MCLK_SOURCE_HSCLK) DL_SYSCTL_setMCLKSource(HSCLK, SYSOSC);
    DL_SYSCTL_disableSYSPLL();
    DL_SYSCTL_disableHFXT();
}

void bsp_clock_init(void) {
    bsp_clock_to_reset_state();
    DL_SYSCTL_setBORThreshold(DL_SYSCTL_BOR_THRESHOLD_LEVEL_0);
    DL_SYSCTL_setFlashWaitState(DL_SYSCTL_FLASH_WAIT_STATE_2);       /* required above 48 MHz (datasheet 7.x) */
    DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_BASE);
    DL_GPIO_initPeripheralAnalogFunction(PIN_HFXIN_IOMUX);
    DL_GPIO_initPeripheralAnalogFunction(PIN_HFXOUT_IOMUX);
    DL_SYSCTL_setHFCLKSourceHFXTParams(DL_SYSCTL_HFXT_RANGE_32_48_MHZ, 100, true);  /* 40 MHz crystal; waits for HFCLKGOOD; startup 100 as in TI's example */
    /* Bench bug 2 (2026-11-25): DL_SYSCTL_configSYSPLL rewrites CPUSS->CTL (icache off) and two instructions later
     * loads a literal from flash. When that function starts on an 8-byte flash line, the load returned the
     * neighbouring instruction word instead of the literal and the next store HardFaulted (PC +0x5a). Put CTL in
     * the state the function sets before calling it, so its own write changes nothing; it restores ctl itself. */
    const uint32_t ctl = DL_CORE_getInstructionConfig();
    DL_CORE_configInstruction(DL_CORE_CACHE_DISABLED, DL_CORE_PREFETCH_ENABLED, DL_CORE_LITERAL_CACHE_ENABLED);
    __ISB();
    DL_SYSCTL_configSYSPLL(&k_pll);
    CPUSS->CTL = ctl;
    __ISB();
    while (!pll_locked_right()) {
        DL_SYSCTL_disableSYSPLL();
        DL_SYSCTL_enableSYSPLL();
        while ((DL_SYSCTL_getClockStatus() & SYSCTL_CLKSTATUS_SYSPLLGOOD_MASK) != DL_SYSCTL_CLK_STATUS_SYSPLL_GOOD) {}
    }
    DL_SYSCTL_setULPCLKDivider(DL_SYSCTL_ULPCLK_DIV_2);
    DL_SYSCTL_setMCLKSource(SYSOSC, HSCLK, DL_SYSCTL_HSCLK_SOURCE_SYSPLL);
    while ((DL_SYSCTL_getClockStatus() & (DL_SYSCTL_CLK_STATUS_HFCLK_GOOD | DL_SYSCTL_CLK_STATUS_LFOSC_GOOD)) !=
           (DL_SYSCTL_CLK_STATUS_HFCLK_GOOD | DL_SYSCTL_CLK_STATUS_LFOSC_GOOD)) {}
}

void bsp_gpio_init(void) {
    DL_GPIO_reset(GPIOA); DL_GPIO_reset(GPIOB);
    DL_GPIO_enablePower(GPIOA); DL_GPIO_enablePower(GPIOB);
    delay_cycles(POWER_STARTUP_DELAY);
    DL_GPIO_initDigitalOutputFeatures(PIN_LED_IOMUX, DL_GPIO_INVERSION_ENABLE, DL_GPIO_RESISTOR_NONE,
                                      DL_GPIO_DRIVE_STRENGTH_LOW, DL_GPIO_HIZ_DISABLE);   /* LED1 is active low */
    DL_GPIO_clearPins(PIN_LED_PORT, PIN_LED);
    DL_GPIO_enableOutput(PIN_LED_PORT, PIN_LED);
    DL_GPIO_initDigitalOutput(PIN_HEAD_IOMUX);
    DL_GPIO_initDigitalOutput(PIN_TURN_IOMUX);
    DL_GPIO_clearPins(PIN_LIGHTS_PORT, PIN_HEAD | PIN_TURN);
    DL_GPIO_enableOutput(PIN_LIGHTS_PORT, PIN_HEAD | PIN_TURN);
    DL_GPIO_initDigitalOutput(PIN_PROBE_IOMUX);
    DL_GPIO_clearPins(PIN_PROBE_PORT, PIN_PROBE);
    DL_GPIO_enableOutput(PIN_PROBE_PORT, PIN_PROBE);
}

/* GPIO first: HFXT pins are muxed in clock init. LED1 is lit only while the clocks come up, so a board that hangs in
 * bsp_clock_init (PLL never locks right, HFXT never starts) shows a solid red LED and prints nothing. */
void bsp_board_init(void) { bsp_gpio_init(); bsp_led(1); bsp_clock_init(); bsp_led(0); }

void bsp_led(int on) { if (on) DL_GPIO_setPins(PIN_LED_PORT, PIN_LED); else DL_GPIO_clearPins(PIN_LED_PORT, PIN_LED); }

void bsp_delay_ms(uint32_t ms) { while (ms--) delay_cycles(CPU_HZ / 1000u); }

void bsp_system_reset(void) { DL_SYSCTL_resetDevice(DL_SYSCTL_RESET_SYSRST); for (;;) {} }

/* WWDT0: once enabled it is write-protected and only a SYSRST stops it (TRM SLAU846 ch. 40). 32768 Hz / 2 / 2^15 = 2.00 s. Closed window 0 %: feeding is allowed any time
 * before expiry (a nonzero closed window would turn an early feed into a reset). Never call DL_WWDT_reset() or
 * DL_WWDT_disablePower() in the app. */
void bsp_wdt_start_2s(void) {
    DL_WWDT_enablePower(WWDT0);
    delay_cycles(POWER_STARTUP_DELAY);
    DL_WWDT_initWatchdogMode(WWDT0, DL_WWDT_CLOCK_DIVIDE_2, DL_WWDT_TIMER_PERIOD_15_BITS, DL_WWDT_RUN_IN_SLEEP,
                             DL_WWDT_WINDOW_PERIOD_0, DL_WWDT_WINDOW_PERIOD_0);
}
/* Images that never start the watchdog (bring-up, self-tests) have an unpowered WWDT: skip the feed. */
void bsp_wdt_feed(void) { if (DL_WWDT_isPowerEnabled(WWDT0)) DL_WWDT_restart(WWDT0); }
