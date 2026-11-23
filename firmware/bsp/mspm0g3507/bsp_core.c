/* App-side board support for the MSPM0G3507: UART0 on the XDS110 backchannel (the log console) and the
 * millisecond clock. NOT YET RUN ON A BOARD.
 * UART0 sits on ULPCLK, which bsp_clock_init() sets to MCLK/2 = 40 MHz (VERIFY the baud rate on a terminal in P2). */
#include <ti/driverlib/driverlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"
#include "bsp_pins.h"

#define UART_CLK_HZ 40000000u

/* FreeRTOS owns SysTick; the tick is 1 ms (FreeRTOSConfig.h). Before the scheduler starts this reads 0. */
uint32_t bsp_millis(void) { return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; }

void bsp_log_init(void) {
    static const DL_UART_Main_ClockConfig clk = { .clockSel = DL_UART_MAIN_CLOCK_BUSCLK, .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1 };
    static const DL_UART_Main_Config cfg = {
        .mode = DL_UART_MAIN_MODE_NORMAL, .direction = DL_UART_MAIN_DIRECTION_TX_RX,
        .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE, .parity = DL_UART_MAIN_PARITY_NONE,
        .wordLength = DL_UART_MAIN_WORD_LENGTH_8_BITS, .stopBits = DL_UART_MAIN_STOP_BITS_ONE,
    };
    DL_UART_Main_reset(UART0);
    DL_UART_Main_enablePower(UART0);
    delay_cycles(16);
    DL_GPIO_initPeripheralOutputFunction(PIN_UART_TX_IOMUX, PIN_UART_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(PIN_UART_RX_IOMUX, PIN_UART_RX_FUNC);
    DL_UART_Main_setClockConfig(UART0, (DL_UART_Main_ClockConfig *)&clk);
    DL_UART_Main_init(UART0, (DL_UART_Main_Config *)&cfg);
    DL_UART_Main_setOversampling(UART0, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_configBaudRate(UART0, UART_CLK_HZ, 115200);
    DL_UART_Main_enableFIFOs(UART0);
    DL_UART_Main_enable(UART0);
}

void bsp_putc(char c) {
    uint32_t t = 100000;
    while (DL_UART_Main_isTXFIFOFull(UART0) && --t) {}
    DL_UART_Main_transmitData(UART0, (uint8_t)c);
}

/* Microseconds since the scheduler started, from the FreeRTOS tick plus the SysTick down-counter (80 counts per us).
 * The tick is read on both sides of the counter so a tick interrupt in between can't pair a new count with an old
 * tick. Wraps every ~71 minutes; callers only take differences. For timing the node task, not for timestamps. */
uint32_t bsp_micros(void) {
    uint32_t t1, v, t2;
    do { t1 = xTaskGetTickCount(); v = SysTick->VAL; t2 = xTaskGetTickCount(); } while (t1 != t2);
    return t1 * 1000u + (SysTick->LOAD - v) / (configCPU_CLOCK_HZ / 1000000u);
}
