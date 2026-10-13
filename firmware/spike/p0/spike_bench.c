/* P0 spike: entry points the emulator harness (bench_emu.py) calls one at a time to count executed instructions.
 * bench_ed25519 checks the golden image's real signature; bench_sha512 hashes a full 48 KB slot body from flash. */
#include <stddef.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "bench_data.h"

#define SLOT_BODY (48u * 1024u - 0x200u)

extern uint32_t _estack;
int main(void);
void Reset_Handler(void) { main(); for (;;) {} }     /* keeps the bench functions alive through --gc-sections */
__attribute__((section(".isr_vector"), used)) static void *const vectors[] = { &_estack, (void *)Reset_Handler };

__attribute__((used, noinline)) int bench_ed25519(void) {
    return crypto_ed25519_check(bench_hdr + 0x60, bench_pk, bench_hdr, 0x60);
}

__attribute__((used, noinline)) int bench_sha512(void) {
    uint8_t h[64];
    crypto_sha512(h, (const uint8_t *)0x00001000u, SLOT_BODY);   /* any 48 KB of mapped flash: cost does not depend on content */
    return h[0];
}

int main(void) { return bench_ed25519() + bench_sha512(); }
