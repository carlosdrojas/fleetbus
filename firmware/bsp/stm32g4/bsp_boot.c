/* Bootloader-only board support for the STM32G431: SysTick as the HAL timebase (no RTOS, keeps TIM/UART out of the
 * bootloader) and the jump into the application. Linked into the bootloader only, never into an app. */
#include "stm32g4xx_hal.h"
#include "bsp.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t prio) {
    if (HAL_SYSTICK_Config(SystemCoreClock / 1000u) != 0u) return HAL_ERROR;
    HAL_NVIC_SetPriority(SysTick_IRQn, prio, 0);
    return HAL_OK;
}
void SysTick_Handler(void) { HAL_IncTick(); }

void bsp_jump_to_app(uint32_t vectors) {
    uint32_t sp = *(volatile uint32_t *)vectors, pc = *(volatile uint32_t *)(vectors + 4);
    __disable_irq();
    HAL_RCC_DeInit();
    HAL_DeInit();
    SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
    for (int i = 0; i < 8; i++) { NVIC->ICER[i] = 0xFFFFFFFF; NVIC->ICPR[i] = 0xFFFFFFFF; }
    SCB->VTOR = vectors;
    __set_MSP(sp);
    ((void (*)(void))pc)();
    for (;;) {}
}
