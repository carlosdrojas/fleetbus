/* App-side board support for the MSPM0G3507: UART0 on the XDS110 backchannel (log + telematics serial link) and the
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

/* Serial link (telematics node <-> gateway Pi) on the same UART0: the LaunchPad's USB port is the Pi cable. */
void bsp_link_write(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) bsp_putc((char)b[i]); }
int bsp_link_read_byte(uint8_t *b) {
    if (DL_UART_Main_isRXFIFOEmpty(UART0)) return 0;
    *b = DL_UART_Main_receiveData(UART0);        /* framing/overrun errors are flagged per byte; the link CRC rejects bad frames */
    return 1;
}
