#pragma once
#include <stddef.h>
#include <stdint.h>

/* Flash abstraction so boot/OTA logic is host-testable against a fake flash with power-loss injection.
 * write() takes an 8-byte-aligned address and a length that is a multiple of 8 (STM32G4 double-word program). */
typedef struct fb_flash {
    int (*read)(void *ctx, uint32_t addr, uint8_t *out, size_t len);
    int (*erase_page)(void *ctx, uint32_t page_addr);
    int (*write)(void *ctx, uint32_t addr, const uint8_t *data, size_t len);
    void *ctx;
} fb_flash_t;

#define FB_FLASH_PAGE 2048u

/* Flash map for STM32G431RB (128 KB). TARGET layout, checked against measured bootloader size in M9. */
#define FB_FLASH_BASE   0x08000000u
#define FB_BOOT_BASE    0x08000000u
#define FB_BOOT_SIZE    (24u * 1024u)
#define FB_META_A_ADDR  0x08006000u
#define FB_META_B_ADDR  0x08006800u
#define FB_SLOT_A_ADDR  0x08007000u
#define FB_SLOT_B_ADDR  0x08013000u
#define FB_SLOT_SIZE    (48u * 1024u)
#define FB_SLOT_ADDR(s) ((s) == 0 ? FB_SLOT_A_ADDR : FB_SLOT_B_ADDR)
