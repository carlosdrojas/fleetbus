#pragma once
/* Board support for the Nucleo-G431RB FleetBus nodes. PIN MAP IS FROM THE DATASHEET/UM2505 AND NOT YET VERIFIED
 * ON A BOARD (hardware has not arrived). Check each against the schematic during bring-up.
 *   LED LD2 .......... PA5          CAN1 RX/TX ....... PA11 / PA12 (AF9) -> SN65HVD230 RXD/TXD
 *   LPUART1 (VCP) .... PA2 / PA3    I2C1 SCL/SDA ..... PB8 / PB9 (AF4)   -> INA219
 *   ADC1_IN1 / IN2 ... PA0 / PA1    (thermistor divider / pot standing in for pack voltage)
 *   Servo PWM ........ PB4 (TIM3_CH1, AF2)   Head / turn lights ... PB0 / PB1 */
#include <stddef.h>
#include <stdint.h>
#include "flash_if.h"

void bsp_clock_init(void);                 /* HSI16 -> PLL -> 170 MHz, voltage boost, 8 flash wait states */
void bsp_gpio_init(void);
void bsp_led(int on);
void bsp_log_init(void);
void bsp_log(const char *fmt, ...);        /* LPUART1 @ 115200 polled; %d %u %x %c %s only (tiny formatter) */
uint32_t bsp_millis(void);
void bsp_iwdg_start_2s(void);
void bsp_iwdg_feed(void);
const fb_flash_t *bsp_flash(void);         /* STM32G4 internal flash behind the fb_flash_t interface */

int  bsp_can_init(void);                   /* FDCAN1 classic CAN 500 kbit/s; sets rx interrupt */
int  bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc);
int  bsp_can_receive(uint32_t *id, uint8_t *data, uint8_t *dlc);   /* non-blocking pop from FIFO0 */
uint32_t bsp_can_error_count(void);

int  bsp_adc_init(void);
int  bsp_adc_read_mv(int channel_1_or_2);
int  bsp_i2c_init(void);
int  bsp_ina219_read_ma(int32_t *ma);      /* 0 ok, -1 not responding */
void bsp_servo_init(void);
void bsp_servo_set_deg(uint8_t deg);       /* 0..180 -> 1.0..2.0 ms pulse */
void bsp_lights_set(uint8_t mask);         /* FB_LIGHT_* */

void bsp_link_write(const uint8_t *b, size_t n);   /* telematics: serial link to the gateway (shares LPUART1 with bsp_log; use one or the other) */
int  bsp_link_read_byte(uint8_t *b);                /* non-blocking; 1 if a byte was read */
