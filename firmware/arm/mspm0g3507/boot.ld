/* MSPM0G3507 bootloader: 28 KB at the start of flash (firmware/boards/mspm0g3507/fb_board.h). SRAM through the
 * unchecked alias 0x2020_0000 (datasheet table 8-5), as TI's own linker files do. */
__stack_reserve = 0xC00;   /* P0 measured 1,888 B peak for the Ed25519 check (emulated M0+); 3 KB with margin */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 28K
  SRAM  (rwx) : ORIGIN = 0x20200000, LENGTH = 32K
}
INCLUDE common.ld
