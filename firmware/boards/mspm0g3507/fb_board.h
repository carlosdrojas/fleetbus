#pragma once
/* Board facts for the TI MSPM0G3507 (LP-MSPM0G3507 LaunchPad): flash map, erase granularity, image hardware id.
 * Selected with -DFB_BOARD=mspm0g3507. Primary target since 2026-10-12. Addresses from the datasheet memory map
 * (SLASEX6C table 8-5, T1); region sizes from docs/p0-feasibility.md. UNVERIFIED ON HARDWARE. */
#define FB_BOARD_NAME   "mspm0g3507"
#define FB_BOARD_HW_ID  0x3507u          /* FB_IMG_HW_MSPM0G3507 */

#define FB_FLASH_PAGE   1024u            /* one sector: the erase unit (TRM SLAU846 table 6-1) */
#define FB_FLASH_SIZE   (128u * 1024u)

/* Same shape as the STM32 map so boot logic, image format and tools change as little as possible.
 * Metadata copies sit inside the first 32 KB, which is rated for 100 k erase cycles (datasheet note on table 8-5). */
#define FB_FLASH_BASE   0x00000000u
#define FB_BOOT_BASE    0x00000000u
#define FB_BOOT_SIZE    (28u * 1024u)
#define FB_META_A_ADDR  0x00007000u      /* one 1 KB sector */
#define FB_META_B_ADDR  0x00007800u      /* one 1 KB sector; 0x7400 and 0x7C00 stay unused */
#define FB_SLOT_A_ADDR  0x00008000u
#define FB_SLOT_B_ADDR  0x00014000u
#define FB_SLOT_SIZE    (48u * 1024u)

/* Erased flash words read back nondeterministic data (TRM 6.3.6), so nothing may treat 0xFF as "erased".
 * The host fake flash fills erased sectors with pseudo-random bytes for this board to prove it. */
#define FB_FLASH_ERASED_DETERMINISTIC 0
