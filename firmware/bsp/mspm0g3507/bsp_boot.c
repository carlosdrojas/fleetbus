/* Bootloader-only board support for the MSPM0G3507: the jump into the application. Linked into the bootloader only.
 * NOT YET RUN ON A BOARD.
 *
 * Cortex-M0+ differences from the STM32 version (P0 R5): VTOR exists on this part (TRM table 3-6) and the 0x200
 * header keeps the app's table 512-byte aligned; the NVIC has a single ICER/ICPR register (32 interrupts), so only
 * index 0 is cleared (ICER[1..7] would be reserved addresses). The watchdog keeps running across the jump. */
#include <ti/driverlib/driverlib.h>
#include "bsp.h"

void bsp_clock_to_reset_state(void);

void bsp_jump_to_app(uint32_t vectors) {
    uint32_t sp = *(volatile uint32_t *)vectors, pc = *(volatile uint32_t *)(vectors + 4);
    __disable_irq();
    SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    bsp_clock_to_reset_state();                   /* app's clock init starts from reset clocks, as after a power-on */
    SCB->VTOR = vectors;
    __DSB(); __ISB();
    __set_MSP(sp);
    __enable_irq();                               /* the app expects PRIMASK clear, as after reset */
    ((void (*)(void))pc)();
    for (;;) {}
}
