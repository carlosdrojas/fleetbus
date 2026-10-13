#include "stm32g4xx_hal.h"
#include "bsp.h"
#include "fb_types.h"
#include "body_fsm.h"

static ADC_HandleTypeDef hadc;
static I2C_HandleTypeDef hi2c;
static TIM_HandleTypeDef htim3;

int bsp_adc_init(void) {
    __HAL_RCC_ADC12_CLK_ENABLE();
    GPIO_InitTypeDef g = {0}; g.Pin = GPIO_PIN_0 | GPIO_PIN_1; g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
    hadc.Instance = ADC1;
    hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4; hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT; hadc.Init.ScanConvMode = ADC_SCAN_DISABLE; hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.ContinuousConvMode = DISABLE; hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START; hadc.Init.NbrOfConversion = 1;
    hadc.Init.OversamplingMode = ENABLE; hadc.Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_16;
    hadc.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4; hadc.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    if (HAL_ADC_Init(&hadc) != HAL_OK) return -1;
    return HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED) == HAL_OK ? 0 : -2;
}

int bsp_adc_read_mv(int ch) {
    ADC_ChannelConfTypeDef c = {0};
    c.Channel = ch == 1 ? ADC_CHANNEL_1 : ADC_CHANNEL_2; c.Rank = ADC_REGULAR_RANK_1;
    c.SamplingTime = ADC_SAMPLETIME_47CYCLES_5; c.SingleDiff = ADC_SINGLE_ENDED; c.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(&hadc, &c) != HAL_OK) return -1;
    HAL_ADC_Start(&hadc);
    if (HAL_ADC_PollForConversion(&hadc, 5) != HAL_OK) return -1;
    int raw = (int)HAL_ADC_GetValue(&hadc);
    HAL_ADC_Stop(&hadc);
    return raw * 3300 / 4095;       /* assumes VDDA = 3.3 V */
}

int bsp_i2c_init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0}; g.Pin = GPIO_PIN_8 | GPIO_PIN_9; g.Mode = GPIO_MODE_AF_OD; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);
    hi2c.Instance = I2C1;
    hi2c.Init.Timing = 0x30A0A7FB;      /* 100 kHz at 170 MHz kernel clock: value from ST CubeMX for G4 (UNVERIFIED on this board) */
    hi2c.Init.OwnAddress1 = 0; hi2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; hi2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&hi2c) == HAL_OK ? 0 : -1;
}

/* INA219 shunt-voltage register (0x01), 10 uV/LSB, 0.1 ohm shunt => 0.1 mA per LSB. Calibration register unused. */
int bsp_ina219_read_ma(int32_t *ma) {
    uint8_t reg = 0x01, rx[2];
    if (HAL_I2C_Master_Transmit(&hi2c, 0x40 << 1, &reg, 1, 5) != HAL_OK) return -1;
    if (HAL_I2C_Master_Receive(&hi2c, 0x40 << 1, rx, 2, 5) != HAL_OK) return -1;
    int16_t raw = (int16_t)((rx[0] << 8) | rx[1]);
    *ma = raw / 10;
    return 0;
}

void bsp_servo_init(void) {
    __HAL_RCC_TIM3_CLK_ENABLE();
    GPIO_InitTypeDef g = {0}; g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &g);
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 170 - 1;              /* 1 MHz */
    htim3.Init.Period = 20000 - 1;               /* 50 Hz */
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    HAL_TIM_PWM_Init(&htim3);
    TIM_OC_InitTypeDef oc = {0}; oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = 1500; oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}
void bsp_servo_set_deg(uint8_t deg) {
    if (deg > 180) deg = 180;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 1000u + (uint32_t)deg * 1000u / 180u);
}
void bsp_lights_set(uint8_t m) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, (m & FB_LIGHT_HEAD) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, (m & FB_LIGHT_TURN) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    bsp_led((m & FB_LIGHT_FAULT) != 0);
}
