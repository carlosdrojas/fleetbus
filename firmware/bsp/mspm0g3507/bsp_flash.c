/* MSPM0G3507 internal flash behind fb_flash_t. Used by the bootloader (metadata only) and the apps (metadata confirm,
 * OTA slot writes). Command sequence from TI's flashctl_program_with_ecc / flashctl_blank_verify examples (SDK 2.11).
 * NOT YET RUN ON A BOARD.
 *
 * Reads go through the ECC-uncorrected alias (flash + 0x0040_0000, datasheet table 8-5). An erased flash word reads
 * back nondeterministic data and, through the corrected path, can raise an uncorrectable-ECC error that resets the
 * chip by default (TRM SLAU846 6.3.6, P0 finding). A fresh board has erased metadata, so the corrected path could
 * reset-loop the bootloader. Through the alias an erased word is just garbage, which the CRC (metadata) and
 * signature + SHA-512 (images) already reject. Cost: a single-bit flash error is not corrected for these reads; it
 * fails the CRC or hash instead, which is the fail-closed direction.
 *
 * Writes: 1 KB sector erase, then each 64-bit word programmed once with hardware-generated ECC (the common code never
 * programs a word twice between erases, so the 83-writes-per-word-line limit cannot be reached, P0 R3).
 * The flash controller runs the command from SRAM (DriverLib ...FromRAM functions in .ramfunc). The single bank
 * stalls instruction fetch until an erase finishes (up to 200 ms, datasheet), so interrupts are delayed, not lost.
 * The bootloader region is refused for erase and write: nothing in FleetBus updates the bootloader in the field. */
#include <string.h>
#include <ti/driverlib/driverlib.h>
#include "bsp.h"

#define ECC_UNCORRECTED_ALIAS 0x00400000u

static int in_flash(uint32_t addr, size_t len) {
    uint32_t off = addr - FB_FLASH_BASE;              /* wraps to a huge value below the base, so one check covers both ends */
    return len <= FB_FLASH_SIZE && off <= FB_FLASH_SIZE - len;
}
static int writable(uint32_t addr, size_t len) {
    return in_flash(addr, len) && addr >= FB_BOOT_BASE + FB_BOOT_SIZE;
}

static int f_read(void *c, uint32_t addr, uint8_t *out, size_t len) {
    (void)c;
    if (!in_flash(addr, len)) return -1;
    memcpy(out, (const void *)(uintptr_t)(addr + ECC_UNCORRECTED_ALIAS), len);
    return 0;
}

static int f_erase(void *c, uint32_t addr) {
    (void)c;
    if (addr % FB_FLASH_PAGE || !writable(addr, FB_FLASH_PAGE)) return -1;
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(FLASHCTL, addr, DL_FLASHCTL_REGION_SELECT_MAIN);
    return DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL, addr, DL_FLASHCTL_COMMAND_SIZE_SECTOR) == DL_FLASHCTL_COMMAND_STATUS_PASSED ? 0 : -1;
}

static int f_write(void *c, uint32_t addr, const uint8_t *d, size_t len) {
    (void)c;
    if (addr % 8 || len % 8 || !writable(addr, len)) return -1;
    for (size_t i = 0; i < len; i += 8) {
        uint32_t w[2];
        memcpy(w, d + i, 8);                          /* source may be unaligned */
        DL_FlashCTL_executeClearStatus(FLASHCTL);
        DL_FlashCTL_unprotectSector(FLASHCTL, addr + i, DL_FLASHCTL_REGION_SELECT_MAIN);
        if (DL_FlashCTL_programMemoryFromRAM64WithECCGenerated(FLASHCTL, addr + i, w) != DL_FLASHCTL_COMMAND_STATUS_PASSED)
            return -1;
    }
    return 0;
}

static const fb_flash_t g_flash = { f_read, f_erase, f_write, 0 };
const fb_flash_t *bsp_flash(void) { return &g_flash; }
