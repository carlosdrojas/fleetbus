#pragma once
#include "flash_if.h"
/* RAM-backed flash with the board's size and page. Each 8-byte word may be programmed once between erases
 * (programming it again is an error). Erase sets 0xFF where the part reads back 0xFF (STM32G4), and pseudo-random
 * bytes where erased reads are nondeterministic (MSPM0, FB_FLASH_ERASED_DETERMINISTIC 0), so no logic can rely on
 * 0xFF meaning "erased". Power loss: after `fail_after` more erase/write ops the next op is torn (half-done) and
 * every later op fails until fake_flash_power_on(). */
#define FAKE_FLASH_SIZE FB_FLASH_SIZE
typedef struct {
    uint8_t mem[FAKE_FLASH_SIZE];
    uint8_t programmed[FAKE_FLASH_SIZE / 8];   /* 1 = word programmed since its last erase */
    uint32_t rng;
    int ops_until_fail;     /* <0 = never */
    int dead;
    int erase_count, write_count;
} fake_flash_t;

void fake_flash_init(fake_flash_t *f, fb_flash_t *iface);
void fake_flash_power_fail_in(fake_flash_t *f, int ops);
void fake_flash_power_on(fake_flash_t *f);
