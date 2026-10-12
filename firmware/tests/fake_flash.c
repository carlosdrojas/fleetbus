#include <string.h>
#include "fake_flash.h"

static int in_range(uint32_t addr, size_t len) { return addr >= FB_FLASH_BASE && addr - FB_FLASH_BASE + len <= FAKE_FLASH_SIZE; }

static int f_read(void *c, uint32_t addr, uint8_t *out, size_t len) {
    fake_flash_t *f = c;
    if (!in_range(addr, len)) return -1;
    memcpy(out, f->mem + (addr - FB_FLASH_BASE), len);
    return 0;
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
    if (t == 1) { memset(p, 0xA5, FB_FLASH_PAGE / 2); memset(p + FB_FLASH_PAGE / 2, 0xFF, FB_FLASH_PAGE / 2); return -1; }
    memset(p, 0xFF, FB_FLASH_PAGE);
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
    for (size_t i = 0; i < len; i++) if (p[i] != 0xFF) return -1;   /* programming non-erased flash */
    memcpy(p, d, n);
    if (t == 1) return -1;
    f->write_count++;
    return 0;
}

void fake_flash_init(fake_flash_t *f, fb_flash_t *iface) {
    memset(f->mem, 0xFF, sizeof f->mem);
    f->ops_until_fail = -1; f->dead = 0; f->erase_count = f->write_count = 0;
    iface->read = f_read; iface->erase_page = f_erase; iface->write = f_write; iface->ctx = f;
}
void fake_flash_power_fail_in(fake_flash_t *f, int ops) { f->ops_until_fail = ops; }
void fake_flash_power_on(fake_flash_t *f) { f->dead = 0; f->ops_until_fail = -1; }
