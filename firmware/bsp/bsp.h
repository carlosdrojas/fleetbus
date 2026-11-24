#pragma once
/* Board support interface (C). Implementation for the LP-MSPM0G3507 LaunchPad in bsp/mspm0g3507/ on TI DriverLib;
 * pin map in bsp/mspm0g3507/bsp_pins.h. The C++ node logic never includes this: only app/ and the bring-up images do.
 * Status per function: docs/measured.md (nothing is "verified" until it has run on the board). */
#include <stddef.h>
#include <stdint.h>

#define FB_BOARD_NAME "mspm0g3507"

#ifdef __cplusplus
extern "C" {
#endif

void bsp_board_init(void);                 /* GPIO, then clocks (LED1 lit only during clock setup): first call in main() */
void bsp_clock_init(void);
void bsp_gpio_init(void);
void bsp_led(int on);
void bsp_delay_ms(uint32_t ms);            /* busy wait; bring-up only */
void bsp_log_init(void);
void bsp_log(const char *fmt, ...);        /* polled UART0 @ 115200 on the XDS110 backchannel; %d %u %x %c %s, l modifier */
void bsp_putc(char c);
uint32_t bsp_millis(void);                 /* FreeRTOS tick (1 ms); 0 before the scheduler starts */
uint32_t bsp_micros(void);                 /* tick + SysTick counter; differences only (wraps ~71 min) */
void bsp_system_reset(void) __attribute__((noreturn));

/* Watchdog (WWDT0): 2 s, cannot be stopped once started (only a reset clears it). The node images start it in main()
 * and feed it at the end of every 10 ms node-task cycle. */
void bsp_wdt_start_2s(void);
void bsp_wdt_feed(void);

int  bsp_can_init(void);                   /* MCAN, classic CAN 2.0A, 500 kbit/s */
int  bsp_can_send(uint32_t id, const uint8_t *data, uint8_t dlc);   /* 0 ok, -1 TX FIFO full or bus-off */
int  bsp_can_receive(uint32_t *id, uint8_t *data, uint8_t *dlc);   /* non-blocking pop from RX FIFO 0; 1 if a frame */
uint32_t bsp_can_error_count(void);
int  bsp_can_set_loopback(int on);         /* bench self-test: TX looped to RX inside the controller */
/* Error state for the fault-injection bench: transmit/receive error counters and whether the controller is bus-off
 * (the driver starts bus-off recovery on the next send/receive poll). */
void bsp_can_counters(uint32_t *tec, uint32_t *rec, int *bus_off);

int  bsp_adc_init(void);
int  bsp_adc_read_mv(int channel_1_or_2);  /* -1 on timeout */
void bsp_servo_init(void);
void bsp_servo_set_deg(uint8_t deg);       /* 0..180 -> 1.0..2.0 ms pulse at 50 Hz */
void bsp_lights_set(uint8_t mask);         /* FB_LIGHT_* (fb_types.h) */

/* 160x128 RGB565 LCD (Adafruit 1.8" ST7735R on SPI1). init returns 0 if the driver is present; the panel itself can't
 * be detected (no read-back on this wiring). */
int  bsp_display_init(void);
void bsp_display_fill(int x, int y, int w, int h, uint16_t rgb565);

#ifdef __cplusplus
}
#endif
