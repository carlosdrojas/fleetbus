#pragma once
#include <stddef.h>
#include <stdint.h>
#include "fb_board.h"      /* per-board flash map: firmware/boards/<FB_BOARD>/fb_board.h */

/* Flash abstraction so boot/OTA logic is host-testable against a fake flash with power-loss injection.
 * write() takes an 8-byte-aligned address and a length that is a multiple of 8: both supported parts program
 * 64-bit words (STM32G4 double-word, MSPM0 flash word). Each word is programmed at most once between erases. */
typedef struct fb_flash {
    int (*read)(void *ctx, uint32_t addr, uint8_t *out, size_t len);
    int (*erase_page)(void *ctx, uint32_t page_addr);
    int (*write)(void *ctx, uint32_t addr, const uint8_t *data, size_t len);
    void *ctx;
} fb_flash_t;

#define FB_SLOT_ADDR(s) ((s) == 0 ? FB_SLOT_A_ADDR : FB_SLOT_B_ADDR)

_Static_assert(FB_META_A_ADDR % FB_FLASH_PAGE == 0 && FB_META_B_ADDR % FB_FLASH_PAGE == 0, "metadata must be page aligned");
_Static_assert(FB_SLOT_A_ADDR % FB_FLASH_PAGE == 0 && FB_SLOT_B_ADDR % FB_FLASH_PAGE == 0, "slots must be page aligned");
_Static_assert(FB_SLOT_SIZE % FB_FLASH_PAGE == 0, "slot size must be whole pages");
_Static_assert(FB_SLOT_B_ADDR + FB_SLOT_SIZE <= FB_FLASH_BASE + FB_FLASH_SIZE, "slot B must fit in flash");
_Static_assert(FB_BOOT_BASE + FB_BOOT_SIZE <= FB_META_A_ADDR, "bootloader overlaps metadata");
