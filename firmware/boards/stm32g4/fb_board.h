#pragma once
/* Board facts for the STM32G431RB (Nucleo-G431RB): flash map, erase granularity, image hardware id.
 * Selected with -DFB_BOARD=stm32g4 (the CMake default). Secondary target since 2026-10-12. */
#define FB_BOARD_NAME   "stm32g4"
#define FB_BOARD_HW_ID  0x4301u          /* FB_IMG_HW_G431: written into every signed image header */

#define FB_FLASH_PAGE   2048u            /* erase unit */
#define FB_FLASH_SIZE   (128u * 1024u)

/* 128 KB, exactly full. Bootloader grew from the 24 KB plan to 28 KB after measuring 24,504 B (99.7 %). */
#define FB_FLASH_BASE   0x08000000u
#define FB_BOOT_BASE    0x08000000u
#define FB_BOOT_SIZE    (28u * 1024u)
#define FB_META_A_ADDR  0x08007000u
#define FB_META_B_ADDR  0x08007800u
#define FB_SLOT_A_ADDR  0x08008000u
#define FB_SLOT_B_ADDR  0x08014000u
#define FB_SLOT_SIZE    (48u * 1024u)

/* Erased flash reads back as 0xFF on this part. */
#define FB_FLASH_ERASED_DETERMINISTIC 1
