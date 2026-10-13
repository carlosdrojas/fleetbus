#include "stm32g4xx_hal.h"
#include "bsp.h"

void bsp_board_init(void) { HAL_Init(); bsp_clock_init(); bsp_gpio_init(); }
void bsp_delay_ms(uint32_t ms) { HAL_Delay(ms); }
void bsp_system_reset(void) { NVIC_SystemReset(); for (;;) {} }

void bsp_clock_init(void) {
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON; osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON; osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = RCC_PLLM_DIV4; osc.PLL.PLLN = 85;                 /* 16/4*85 = 340 MHz VCO */
    osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = RCC_PLLQ_DIV2; osc.PLL.PLLR = RCC_PLLR_DIV2;   /* 170 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) for (;;) {}
    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1; clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_8) != HAL_OK) for (;;) {}
    RCC_PeriphCLKInitTypeDef pc = {0};
    pc.PeriphClockSelection = RCC_PERIPHCLK_FDCAN | RCC_PERIPHCLK_LPUART1 | RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_ADC12;
    pc.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;               /* 170 MHz kernel clock for the CAN bit timing */
    pc.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_PCLK1;
    pc.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    pc.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    HAL_RCCEx_PeriphCLKConfig(&pc);
}

void bsp_gpio_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1; HAL_GPIO_Init(GPIOB, &g);   /* head, turn lights */
}
void bsp_led(int on) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }

void bsp_wdt_start_2s(void) {    /* IWDG */
    IWDG->KR = 0xCCCC;                          /* start (cannot be stopped until reset) */
    IWDG->KR = 0x5555;                          /* unlock PR/RLR */
    IWDG->PR = 4;                               /* /64: LSI 32 kHz -> 500 Hz */
    IWDG->RLR = 1000;                           /* 2 s */
    while (IWDG->SR) {}
    IWDG->KR = 0xAAAA;
}
void bsp_wdt_feed(void) { IWDG->KR = 0xAAAA; }
