/* P0 spike (docs/mspm0-port-plan.md): the bootloader's portable core with no vendor HAL, so the same code can be linked for
 * Cortex-M4 and Cortex-M0+ and compared byte for byte. Flash is a memory-mapped stub. Not a bootloader: it never jumps. */
#include <string.h>
#include "boot_logic.h"
#include "../../keys/fleetbus_pub.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

void Reset_Handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    main();
    for (;;) {}
}

__attribute__((section(".isr_vector"), used)) static void *const vectors[] = { &_estack, (void *)Reset_Handler };

static int rd(void *c, uint32_t a, uint8_t *o, size_t n) { (void)c; memcpy(o, (const void *)(uintptr_t)a, n); return 0; }
static volatile uint32_t sink;
static int er(void *c, uint32_t a) { (void)c; sink = a; return 0; }
static int wr(void *c, uint32_t a, const uint8_t *d, size_t n) { (void)c; sink = a + d[0] + (uint32_t)n; return 0; }

int main(void) {
    static const fb_flash_t fl = { rd, er, wr, 0 };
    fb_boot_result_t r;
    return fb_boot_run(&fl, 1, FB_PUBKEY, &r);
}
