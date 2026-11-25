/* Section layout for every MSPM0G3507 image (included after the MEMORY block in app.ld.in). Symbol names match TI's
 * startup_mspm0g350x_gcc.c. libgcc stays linked: the Cortex-M0+ has no divide instruction and needs the __aeabi_*
 * helpers. Every block loaded to flash is padded to 8 bytes: the MSPM0 flash programmer writes 64-bit words and
 * rejects a 4-byte block (DSLite: "Length of block is 4, but it should be divisible by 8", found on the bench).
 * .init_array is kept for C++ static constructors (the firmware has none of its own; see README.md). */
ENTRY(Reset_Handler)
__StackTop = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
  .intvecs : { . = ALIGN(4); KEEP(*(.intvecs)) . = ALIGN(4); } > FLASH
  .text : { *(.text) *(.text*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .rodata : { *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); . = ALIGN(8); } > FLASH

  .ramfunct : { . = ALIGN(8); __ramfunct_start__ = .; *(.ramfunc*) *(.TI.ramfunc*) . = ALIGN(8); __ramfunct_end__ = .; } > SRAM AT > FLASH
  __ramfunct_load__ = LOADADDR(.ramfunct);
  .data : ALIGN(8) { __data_start__ = .; *(.data) *(.data*) . = ALIGN(8); __data_end__ = .; } > SRAM AT > FLASH
  __data_load__ = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(4); __bss_start__ = .; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); __bss_end__ = .; } > SRAM
  .stack_reserve (NOLOAD) : { . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .); . = . + __stack_reserve; . = ALIGN(8); } > SRAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
