#pragma once
/* Board support interface shared by every board. Implementations live in bsp/<board>/ (FB_BOARD):
 *   bsp/stm32g4/     Nucleo-G431RB (STM32 HAL)          secondary target
 *   bsp/mspm0g3507/  LP-MSPM0G3507 LaunchPad (TI DriverLib)   primary target since 2026-10-12
 * Pin maps are in each board's bsp_pins.h. NOTHING HERE HAS RUN ON A BOARD YET. */
#include <stddef.h>
#include <stdint.h>
#include "flash_if.h"

void bsp_board_init(void);                 /* vendor HAL init + clocks + GPIO: first call in every main() */
void bsp_clock_init(void);
void bsp_gpio_init(void);
void bsp_led(int on);
void bsp_delay_ms(uint32_t ms);            /* busy wait; bootloader and bring-up only */
void bsp_log_init(void);
void bsp_log(const char *fmt, ...);        /* polled UART @ 115200; %d %u %x %c %s only (bsp/bsp_log.c) */
void bsp_putc(char c);                     /* board: one byte out of the log UART, polled */
uint32_t bsp_millis(void);
void bsp_system_reset(void) __attribute__((noreturn));

/* Watchdog: 2 s timeout, started by the bootloader just before the jump; cannot be stopped until reset on either
 * board, so the app inherits it and must keep feeding. The node core stops feeding on a missed health deadline. */
void bsp_wdt_start_2s(void);
void bsp_wdt_feed(void);

const fb_flash_t *bsp_flash(void);         /* internal flash behind the fb_flash_t interface */
/* Bootloader only: hand the CPU to the application whose vector table is at `vectors` (slot base + 0x200). */
void bsp_jump_to_app(uint32_t vectors) __attribute__((noreturn));

int  bsp_can_init(void);                   /* classic CAN 500 kbit/s */
int  bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc);
int  bsp_can_receive(uint32_t *id, uint8_t *data, uint8_t *dlc);   /* non-blocking pop from the RX FIFO */
uint32_t bsp_can_error_count(void);
int  bsp_can_set_loopback(int on);         /* bench self-test: TX looped to RX inside the controller; -1 if unsupported */

int  bsp_adc_init(void);
int  bsp_adc_read_mv(int channel_1_or_2);
int  bsp_i2c_init(void);
int  bsp_ina219_read_ma(int32_t *ma);      /* 0 ok, -1 not responding */
void bsp_servo_init(void);
void bsp_servo_set_deg(uint8_t deg);       /* 0..180 -> 1.0..2.0 ms pulse */
void bsp_lights_set(uint8_t mask);         /* FB_LIGHT_* */

void bsp_link_write(const uint8_t *b, size_t n);   /* telematics: serial link to the gateway (shares the log UART; use one or the other) */
int  bsp_link_read_byte(uint8_t *b);                /* non-blocking; 1 if a byte was read */
