#pragma once
/* LP-MSPM0G3507 LaunchPad pin map for FleetBus. NOT YET VERIFIED ON A BOARD.
 * Sources: TI LP-MSPM0G3507 example configs in MSPM0 SDK 2.11 (LED, HFXT, UART0, MCAN pins), datasheet SLASEX6C
 * pin table (ADC channels). Check every line against the LaunchPad schematic (SLAU873) during P2-P4 bring-up.
 *
 *   Status LED (LED1, red, active low) ... PA0                 IOMUX_PINCM1
 *   HFXT 40 MHz crystal ................... PA5 / PA6           PINCM10 / PINCM11   (CAN clock source)
 *   UART0 TX / RX (XDS110 backchannel) .... PA10 / PA11         PINCM21 / PINCM22   (jumpers J101 must be fitted)
 *   MCAN0 CAN_TX / CAN_RX ................. PA12 / PA13         PINCM34 / PINCM35   -> transceiver TXD / RXD
 *   ADC0 ch2 thermistor divider ........... PA25 (A0_2)
 *   ADC0 ch3 pot standing in for pack V ... PA24 (A0_3)
 *   Servo PWM (TIMA1 CCP0) ................ PB4                 PINCM17
 *   Head light / turn light ............... PB27 / PB22         PINCM58 / PINCM50   (LED2 green / blue on the LaunchPad)
 *   Adafruit 1.8" TFT (ST7735R, product 358) on the body node, Lite option 4. Breakout pin -> LaunchPad:
 *     VCC -> 3V3   GND -> GND   SCK -> PB9 (SPI1)   MOSI -> PB8 (SPI1)   TFT_CS -> PB6   D/C -> PB16   RESET -> PB15
 *     LITE -> 3V3 (backlight PWM input; tie high for full brightness)   MISO, CARD_CS -> not connected (no SD card)
 *     SPI1 pins as in TI's LP-MSPM0G3507 spi_controller example. PA13 is MCAN RX here: don't reuse it for the LCD.
 */
#include <ti/devices/msp/msp.h>

#define PIN_LED_PORT      GPIOA
#define PIN_LED           DL_GPIO_PIN_0
#define PIN_LED_IOMUX     IOMUX_PINCM1

#define PIN_HFXIN_IOMUX   IOMUX_PINCM10
#define PIN_HFXOUT_IOMUX  IOMUX_PINCM11

#define PIN_UART_TX_IOMUX IOMUX_PINCM21
#define PIN_UART_TX_FUNC  IOMUX_PINCM21_PF_UART0_TX
#define PIN_UART_RX_IOMUX IOMUX_PINCM22
#define PIN_UART_RX_FUNC  IOMUX_PINCM22_PF_UART0_RX

#define PIN_CAN_TX_IOMUX  IOMUX_PINCM34
#define PIN_CAN_TX_FUNC   IOMUX_PINCM34_PF_CANFD0_CANTX
#define PIN_CAN_RX_IOMUX  IOMUX_PINCM35
#define PIN_CAN_RX_FUNC   IOMUX_PINCM35_PF_CANFD0_CANRX


#define ADC_CH_THERM      DL_ADC12_INPUT_CHAN_2
#define ADC_CH_PACK       DL_ADC12_INPUT_CHAN_3

#define PIN_SERVO_IOMUX   IOMUX_PINCM17
#define PIN_SERVO_FUNC    IOMUX_PINCM17_PF_TIMA1_CCP0

#define PIN_LIGHTS_PORT   GPIOB
#define PIN_HEAD          DL_GPIO_PIN_27
#define PIN_HEAD_IOMUX    IOMUX_PINCM58
#define PIN_TURN          DL_GPIO_PIN_22
#define PIN_TURN_IOMUX    IOMUX_PINCM50

#define PIN_LCD_SCK_IOMUX  IOMUX_PINCM26
#define PIN_LCD_SCK_FUNC   IOMUX_PINCM26_PF_SPI1_SCLK
#define PIN_LCD_MOSI_IOMUX IOMUX_PINCM25
#define PIN_LCD_MOSI_FUNC  IOMUX_PINCM25_PF_SPI1_PICO
#define PIN_LCD_PORT       GPIOB
#define PIN_LCD_CS         DL_GPIO_PIN_6
#define PIN_LCD_CS_IOMUX   IOMUX_PINCM23
#define PIN_LCD_DC         DL_GPIO_PIN_16
#define PIN_LCD_DC_IOMUX   IOMUX_PINCM33
#define PIN_LCD_RST        DL_GPIO_PIN_15
#define PIN_LCD_RST_IOMUX  IOMUX_PINCM32
