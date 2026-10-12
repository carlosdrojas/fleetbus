/* App-side board support: HAL timebase on TIM6 (FreeRTOS owns SysTick) and a tiny polled logger on LPUART1.
 * The logger avoids newlib's printf and HAL_UART to save flash (measured: the app slot is tight). */
#include <stdarg.h>
#include <string.h>
#include "stm32g4xx_hal.h"
#include "bsp.h"

static TIM_HandleTypeDef htim6;

HAL_StatusTypeDef HAL_InitTick(uint32_t prio) {
    __HAL_RCC_TIM6_CLK_ENABLE();
    htim6.Instance = TIM6;
    htim6.Init.Prescaler = (HAL_RCC_GetPCLK1Freq() / 1000000u) - 1u;      /* 1 MHz */
    htim6.Init.Period = 1000u - 1u;                                       /* 1 kHz */
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) return HAL_ERROR;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, prio, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    return HAL_TIM_Base_Start_IT(&htim6);
}
void TIM6_DAC_IRQHandler(void) { HAL_TIM_IRQHandler(&htim6); }
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *h) { if (h->Instance == TIM6) HAL_IncTick(); }
uint32_t bsp_millis(void) { return HAL_GetTick(); }

void bsp_log_init(void) {
    __HAL_RCC_LPUART1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = GPIO_AF12_LPUART1;
    HAL_GPIO_Init(GPIOA, &g);
    LPUART1->CR1 = 0;
    LPUART1->BRR = (uint32_t)(((uint64_t)HAL_RCC_GetPCLK1Freq() * 256u + 57600u) / 115200u);   /* 8N1 */
    LPUART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}
static void putc_(char c) {
    uint32_t t = 100000;
    while (!(LPUART1->ISR & USART_ISR_TXE_TXFNF) && --t) {}
    LPUART1->TDR = (uint8_t)c;
}
static void put_num(unsigned long v, unsigned base, int neg) {
    char b[12]; int n = 0;
    do { b[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v && n < 11);
    if (neg) putc_('-');
    while (n) putc_(b[--n]);
}
/* Supports %d %u %x %c %s %% with optional 'l'. Enough for status lines; not a general printf. */
void bsp_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { putc_(*fmt); continue; }
        fmt++; if (*fmt == 'l') fmt++;
        switch (*fmt) {
        case 'd': { long v = va_arg(ap, long); put_num(v < 0 ? (unsigned long)-v : (unsigned long)v, 10, v < 0); break; }
        case 'u': put_num(va_arg(ap, unsigned long), 10, 0); break;
        case 'x': put_num(va_arg(ap, unsigned long), 16, 0); break;
        case 'c': putc_((char)va_arg(ap, int)); break;
        case 's': { const char *s = va_arg(ap, const char *); while (s && *s) putc_(*s++); break; }
        case '%': putc_('%'); break;
        default: putc_('?'); break;
        }
    }
    va_end(ap);
}
