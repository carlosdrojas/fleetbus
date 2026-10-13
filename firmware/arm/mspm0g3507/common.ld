/* Section layout shared by the MSPM0G3507 bootloader and apps (included after a MEMORY block that defines FLASH and
 * SRAM). Symbol names match TI's startup_mspm0g350x_gcc.c. Unlike the STM32 app script, libgcc is NOT discarded:
 * Cortex-M0+ has no divide instruction and needs the __aeabi_* helpers (P0 R1). */
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
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH

  .ramfunct : { . = ALIGN(8); __ramfunct_start__ = .; *(.ramfunc*) *(.TI.ramfunc*) . = ALIGN(8); __ramfunct_end__ = .; } > SRAM AT > FLASH
  __ramfunct_load__ = LOADADDR(.ramfunct);
  .data : { . = ALIGN(4); __data_start__ = .; *(.data) *(.data*) . = ALIGN(4); __data_end__ = .; } > SRAM AT > FLASH
  __data_load__ = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(4); __bss_start__ = .; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); __bss_end__ = .; } > SRAM
  .stack_reserve (NOLOAD) : { . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .); . = . + __stack_reserve; . = ALIGN(8); } > SRAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
