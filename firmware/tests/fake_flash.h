#pragma once
#include "flash_if.h"
/* RAM-backed 128 KB flash. Erase sets 0xFF; write may only clear bits (flash semantics: writing over non-0xFF
 * data without erase is an error). Power loss: after `fail_after` more erase/write ops the next op is torn
 * (half-done) and every later op fails until fake_flash_power_on(). */
#define FAKE_FLASH_SIZE (128u * 1024u)
typedef struct {
    uint8_t mem[FAKE_FLASH_SIZE];
    int ops_until_fail;     /* <0 = never */
    int dead;
    int erase_count, write_count;
} fake_flash_t;

void fake_flash_init(fake_flash_t *f, fb_flash_t *iface);
void fake_flash_power_fail_in(fake_flash_t *f, int ops);
void fake_flash_power_on(fake_flash_t *f);
