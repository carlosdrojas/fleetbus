/* ST7735 160x128 SPI LCD on the body node (FleetBus Lite option 4). NOT YET RUN ON A BOARD.
 * Init sequence: the standard ST7735R one (Sitronix datasheet / the EE319K ST7735 driver lineage), RGB565, landscape.
 * Panels differ between "tabs" and clones: if the picture is mirrored, rotated or offset, or red and blue are
 * swapped, change LCD_MADCTL / LCD_X_OFF / LCD_Y_OFF below (P-L3 bench step). SPI1 at ~8 MHz assuming an 80 MHz
 * bus clock (VERIFY which clock SPI1 runs from; at 40 MHz it is 4 MHz, still fine). */
#include <ti/driverlib/driverlib.h>
#include "bsp.h"
#include "bsp_pins.h"

#define LCD_MADCTL 0xA8u    /* MY | MV (landscape) | BGR. Try 0x68 / 0xA0 / 0x60 if wrong. */
#define LCD_X_OFF 0
#define LCD_Y_OFF 0

static void cs(int lo) { if (lo) DL_GPIO_clearPins(PIN_LCD_PORT, PIN_LCD_CS); else DL_GPIO_setPins(PIN_LCD_PORT, PIN_LCD_CS); }
static void dc(int data) { if (data) DL_GPIO_setPins(PIN_LCD_PORT, PIN_LCD_DC); else DL_GPIO_clearPins(PIN_LCD_PORT, PIN_LCD_DC); }
static void tx(uint8_t b) { DL_SPI_transmitDataBlocking8(SPI1, b); }
static void drain(void) { while (DL_SPI_isBusy(SPI1)) {} }

static void cmd(uint8_t c, const uint8_t *d, int n) {
    cs(1); dc(0); tx(c); drain();
    if (n) { dc(1); for (int i = 0; i < n; i++) tx(d[i]); drain(); }
    cs(0);
}

int bsp_display_init(void) {
    static const DL_SPI_Config cfg = { .mode = DL_SPI_MODE_CONTROLLER, .frameFormat = DL_SPI_FRAME_FORMAT_MOTO3_POL0_PHA0,
        .parity = DL_SPI_PARITY_NONE, .dataSize = DL_SPI_DATA_SIZE_8, .bitOrder = DL_SPI_BIT_ORDER_MSB_FIRST };
    static const DL_SPI_ClockConfig clk = { .clockSel = DL_SPI_CLOCK_BUSCLK, .divideRatio = DL_SPI_CLOCK_DIVIDE_RATIO_1 };
    DL_SPI_reset(SPI1); DL_SPI_enablePower(SPI1); delay_cycles(16);
    DL_GPIO_initPeripheralOutputFunction(PIN_LCD_SCK_IOMUX, PIN_LCD_SCK_FUNC);
    DL_GPIO_initPeripheralOutputFunction(PIN_LCD_MOSI_IOMUX, PIN_LCD_MOSI_FUNC);
    DL_GPIO_initDigitalOutput(PIN_LCD_CS_IOMUX); DL_GPIO_initDigitalOutput(PIN_LCD_DC_IOMUX); DL_GPIO_initDigitalOutput(PIN_LCD_RST_IOMUX);
    DL_GPIO_setPins(PIN_LCD_PORT, PIN_LCD_CS | PIN_LCD_DC | PIN_LCD_RST);
    DL_GPIO_enableOutput(PIN_LCD_PORT, PIN_LCD_CS | PIN_LCD_DC | PIN_LCD_RST);
    DL_SPI_setClockConfig(SPI1, (DL_SPI_ClockConfig *)&clk);
    DL_SPI_init(SPI1, (DL_SPI_Config *)&cfg);
    DL_SPI_setBitRateSerialClockDivider(SPI1, 4);           /* SCLK = clk / ((4 + 1) * 2) */
    DL_SPI_enable(SPI1);

    DL_GPIO_clearPins(PIN_LCD_PORT, PIN_LCD_RST); bsp_delay_ms(10);
    DL_GPIO_setPins(PIN_LCD_PORT, PIN_LCD_RST); bsp_delay_ms(120);
    static const uint8_t frm[] = { 0x01, 0x2C, 0x2D }, frm3[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D }, inv[] = { 0x07 },
        pw1[] = { 0xA2, 0x02, 0x84 }, pw2[] = { 0xC5 }, pw3[] = { 0x0A, 0x00 }, pw4[] = { 0x8A, 0x2A }, pw5[] = { 0x8A, 0xEE },
        vm[] = { 0x0E }, mad[] = { LCD_MADCTL }, col[] = { 0x05 };
    cmd(0x01, 0, 0); bsp_delay_ms(150);                      /* SWRESET */
    cmd(0x11, 0, 0); bsp_delay_ms(255);                      /* SLPOUT */
    cmd(0xB1, frm, 3); cmd(0xB2, frm, 3); cmd(0xB3, frm3, 6); cmd(0xB4, inv, 1);
    cmd(0xC0, pw1, 3); cmd(0xC1, pw2, 1); cmd(0xC2, pw3, 2); cmd(0xC3, pw4, 2); cmd(0xC4, pw5, 2); cmd(0xC5, vm, 1);
    cmd(0x20, 0, 0);                                         /* INVOFF */
    cmd(0x36, mad, 1);                                       /* MADCTL */
    cmd(0x3A, col, 1); bsp_delay_ms(10);                     /* COLMOD 16-bit */
    cmd(0x13, 0, 0); bsp_delay_ms(10);                       /* NORON */
    cmd(0x29, 0, 0); bsp_delay_ms(100);                      /* DISPON */
    return 0;
}

void bsp_display_fill(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    int x0 = x + LCD_X_OFF, y0 = y + LCD_Y_OFF, x1 = x0 + w - 1, y1 = y0 + h - 1;
    uint8_t ca[] = { 0, (uint8_t)x0, 0, (uint8_t)x1 }, ra[] = { 0, (uint8_t)y0, 0, (uint8_t)y1 };
    cmd(0x2A, ca, 4); cmd(0x2B, ra, 4);                      /* CASET, RASET */
    cs(1); dc(0); tx(0x2C); drain(); dc(1);                  /* RAMWR, then pixels */
    for (int32_t n = (int32_t)w * h; n > 0; n--) { tx((uint8_t)(c >> 8)); tx((uint8_t)c); }
    drain(); cs(0);
}
