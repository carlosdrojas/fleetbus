/* FleetBus bootloader (24 KB budget at 0x08000000). On every reset: verify slots (Ed25519 + SHA-512 + anti-downgrade),
 * pick A/B, count the trial attempt, arm the IWDG, jump. If nothing is bootable it blinks LD2 and waits.
 * UNVERIFIED ON HARDWARE. Shared decision logic is host-tested (common/boot_logic.c, 67+ Unity tests). */
#include "stm32g4xx_hal.h"
#include "bsp.h"
#include "boot_logic.h"
#include "image.h"
#include "../keys/fleetbus_pub.h"

#ifndef BOOT_NODE_ID
#error "BOOT_NODE_ID (1 battery, 2 body, 3 telematics) must be defined: the bootloader only boots images built for this node"
#endif

static void jump_to_app(uint32_t addr) {
    uint32_t sp = *(volatile uint32_t *)addr, pc = *(volatile uint32_t *)(addr + 4);
    __disable_irq();
    HAL_RCC_DeInit();
    HAL_DeInit();
    SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
    for (int i = 0; i < 8; i++) { NVIC->ICER[i] = 0xFFFFFFFF; NVIC->ICPR[i] = 0xFFFFFFFF; }
    SCB->VTOR = addr;
    __set_MSP(sp);
    ((void (*)(void))pc)();
    for (;;) {}
}

int main(void) {
    HAL_Init();
    bsp_clock_init();                       /* 170 MHz: signature verification is the slow part */
    bsp_gpio_init();
    fb_boot_result_t r;
    if (fb_boot_run(bsp_flash(), BOOT_NODE_ID, FB_PUBKEY, &r) == 0) {
        bsp_iwdg_start_2s();                /* from here the app must keep feeding it; the node core stops feeding on a missed health deadline */
        jump_to_app(FB_SLOT_ADDR(r.slot) + FB_IMG_HDR_SIZE);
    }
    for (;;) { bsp_led(1); HAL_Delay(100); bsp_led(0); HAL_Delay(100); }   /* nothing bootable */
}
