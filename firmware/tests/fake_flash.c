#include <string.h>
#include "fake_flash.h"

static int in_range(uint32_t addr, size_t len) { return addr >= FB_FLASH_BASE && addr - FB_FLASH_BASE + len <= FAKE_FLASH_SIZE; }

static int f_read(void *c, uint32_t addr, uint8_t *out, size_t len) {
    fake_flash_t *f = c;
    if (!in_range(addr, len)) return -1;
    memcpy(out, f->mem + (addr - FB_FLASH_BASE), len);
    return 0;
}

static void erased_fill(fake_flash_t *f, uint8_t *p, size_t n) {
#if FB_FLASH_ERASED_DETERMINISTIC
    (void)f; memset(p, 0xFF, n);
#else
    for (size_t i = 0; i < n; i++) { f->rng ^= f->rng << 13; f->rng ^= f->rng >> 17; f->rng ^= f->rng << 5; p[i] = (uint8_t)f->rng; }
#endif
}
static void mark(fake_flash_t *f, uint32_t addr, size_t len, uint8_t v) {
    memset(f->programmed + (addr - FB_FLASH_BASE) / 8, v, (len + 7) / 8);
}

static int tick(fake_flash_t *f) {   /* returns 1 if this op is torn by power loss */
    if (f->dead) return -1;
    if (f->ops_until_fail == 0) { f->dead = 1; return 1; }
    if (f->ops_until_fail > 0) f->ops_until_fail--;
    return 0;
}

static int f_erase(void *c, uint32_t addr) {
    fake_flash_t *f = c;
    if (addr % FB_FLASH_PAGE || !in_range(addr, FB_FLASH_PAGE)) return -1;
    int t = tick(f);
    if (t < 0) return -1;
    uint8_t *p = f->mem + (addr - FB_FLASH_BASE);
    if (t == 1) {                                 /* torn erase: first half keeps old-looking junk and still needs an erase */
        memset(p, 0xA5, FB_FLASH_PAGE / 2); mark(f, addr, FB_FLASH_PAGE / 2, 1);
        erased_fill(f, p + FB_FLASH_PAGE / 2, FB_FLASH_PAGE / 2); mark(f, addr + FB_FLASH_PAGE / 2, FB_FLASH_PAGE / 2, 0);
        return -1;
    }
    erased_fill(f, p, FB_FLASH_PAGE);
    mark(f, addr, FB_FLASH_PAGE, 0);
    f->erase_count++;
    return 0;
}

static int f_write(void *c, uint32_t addr, const uint8_t *d, size_t len) {
    fake_flash_t *f = c;
    if (addr % 8 || len % 8 || !in_range(addr, len)) return -1;
    int t = tick(f);
    if (t < 0) return -1;
    uint8_t *p = f->mem + (addr - FB_FLASH_BASE);
    size_t n = t == 1 ? len / 2 : len;           /* torn write: only the first half lands */
    for (size_t w = 0; w < len / 8; w++) if (f->programmed[(addr - FB_FLASH_BASE) / 8 + w]) return -1;   /* programmed twice */
#if FB_FLASH_ERASED_DETERMINISTIC
    for (size_t i = 0; i < len; i++) if (p[i] != 0xFF) return -1;   /* also catches tests that poke mem directly */
#endif
    memcpy(p, d, n);
    mark(f, addr, len, 1);
    if (t == 1) return -1;
    f->write_count++;
    return 0;
}

void fake_flash_init(fake_flash_t *f, fb_flash_t *iface) {
    f->rng = 0x9E3779B9u;
    erased_fill(f, f->mem, sizeof f->mem);
    memset(f->programmed, 0, sizeof f->programmed);
    f->ops_until_fail = -1; f->dead = 0; f->erase_count = f->write_count = 0;
    iface->read = f_read; iface->erase_page = f_erase; iface->write = f_write; iface->ctx = f;
}
void fake_flash_power_fail_in(fake_flash_t *f, int ops) { f->ops_until_fail = ops; }
void fake_flash_power_on(fake_flash_t *f) { f->dead = 0; f->ops_until_fail = -1; }
