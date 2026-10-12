/* The bootloader has no RTOS: plain SysTick as the HAL timebase, nothing else (keeps TIM/UART out of the 24 KB budget). */
#include "stm32g4xx_hal.h"
HAL_StatusTypeDef HAL_InitTick(uint32_t prio) {
    if (HAL_SYSTICK_Config(SystemCoreClock / 1000u) != 0u) return HAL_ERROR;
    HAL_NVIC_SetPriority(SysTick_IRQn, prio, 0);
    return HAL_OK;
}
void SysTick_Handler(void) { HAL_IncTick(); }
