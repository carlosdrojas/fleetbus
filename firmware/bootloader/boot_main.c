/* FleetBus bootloader (28 KB at the start of flash, same on both boards). On every reset: verify slots
 * (Ed25519 + SHA-512 + anti-downgrade), pick A/B, count the trial attempt, arm the 2 s watchdog, jump.
 * If nothing is bootable it blinks the LED and waits. Board specifics (clock, flash driver, watchdog, jump) are in
 * bsp/<board>/. UNVERIFIED ON HARDWARE. Shared decision logic is host-tested (common/boot_logic.c). */
#include "bsp.h"
#include "boot_logic.h"
#include "image.h"
#include "../keys/fleetbus_pub.h"

#ifndef BOOT_NODE_ID
#error "BOOT_NODE_ID (1 battery, 2 body, 3 telematics) must be defined: the bootloader only boots images built for this node"
#endif

int main(void) {
    bsp_board_init();                       /* full speed: signature verification is the slow part (P0: ~0.43 s on M0+) */
    fb_boot_result_t r;
    if (fb_boot_run(bsp_flash(), BOOT_NODE_ID, FB_PUBKEY, &r) == 0) {
        bsp_wdt_start_2s();                 /* from here the app must keep feeding it */
        bsp_jump_to_app(FB_SLOT_ADDR(r.slot) + FB_IMG_HDR_SIZE);
    }
    for (;;) { bsp_led(1); bsp_delay_ms(100); bsp_led(0); bsp_delay_ms(100); }   /* nothing bootable */
}
