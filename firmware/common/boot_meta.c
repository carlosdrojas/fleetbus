#include <string.h>
#include "boot_meta.h"
#include "crc32.h"

#define META_STORED 40u

void fb_meta_default(fb_boot_meta_t *m) {
    memset(m, 0, sizeof *m);
    m->magic = FB_META_MAGIC;
    m->active = 0;
}

static uint32_t meta_crc(const fb_boot_meta_t *m) {
    return fb_crc32c((const uint8_t *)m, offsetof(fb_boot_meta_t, crc));
}

static int read_copy(const fb_flash_t *fl, uint32_t addr, fb_boot_meta_t *m) {
    uint8_t raw[META_STORED];
    if (fl->read(fl->ctx, addr, raw, sizeof raw) != 0) return -1;
    memcpy(m, raw, sizeof *m);
    if (m->magic != FB_META_MAGIC || m->crc != meta_crc(m)) return -1;
    return 0;
}

int fb_meta_load(const fb_flash_t *fl, fb_boot_meta_t *m) {
    fb_boot_meta_t a, b;
    int va = read_copy(fl, FB_META_A_ADDR, &a) == 0;
    int vb = read_copy(fl, FB_META_B_ADDR, &b) == 0;
    if (va && vb) { *m = (a.seq >= b.seq) ? a : b; return 0; }
    if (va) { *m = a; return 0; }
    if (vb) { *m = b; return 0; }
    fb_meta_default(m);
    return -1;
}

int fb_meta_store(const fb_flash_t *fl, fb_boot_meta_t *m) {
    fb_boot_meta_t a, b;
    int va = read_copy(fl, FB_META_A_ADDR, &a) == 0;
    int vb = read_copy(fl, FB_META_B_ADDR, &b) == 0;
    uint32_t target;
    if (va && vb) target = (a.seq >= b.seq) ? FB_META_B_ADDR : FB_META_A_ADDR;   /* overwrite the older */
    else if (va) target = FB_META_B_ADDR;
    else if (vb) target = FB_META_A_ADDR;
    else target = FB_META_A_ADDR;

    uint32_t newest = va && vb ? (a.seq > b.seq ? a.seq : b.seq) : va ? a.seq : vb ? b.seq : 0;
    m->magic = FB_META_MAGIC;
    m->seq = newest + 1;
    m->crc = meta_crc(m);
    uint8_t raw[META_STORED];
    memset(raw, 0xFF, sizeof raw);
    memcpy(raw, m, sizeof *m);
    if (fl->erase_page(fl->ctx, target) != 0) return -1;
    if (fl->write(fl->ctx, target, raw, sizeof raw) != 0) return -2;
    return 0;
}

int fb_boot_decide(fb_boot_meta_t *m, int (*image_ok)(void *, int), void *ctx) {
    int order[2] = { m->active ? 1 : 0, m->active ? 0 : 1 };
    for (int i = 0; i < 2; i++) {
        int s = order[i];
        uint8_t st = m->status[s];
        if (st == FB_SLOT_EMPTY || st == FB_SLOT_BAD) continue;
        if (!image_ok(ctx, s)) { m->status[s] = FB_SLOT_BAD; continue; }
        if (st == FB_SLOT_NEW) { m->status[s] = FB_SLOT_TRIAL; m->attempts = 0; }
        if (m->status[s] == FB_SLOT_TRIAL) {
            if (m->attempts >= FB_BOOT_MAX_ATTEMPTS) { m->status[s] = FB_SLOT_BAD; continue; }
            m->attempts++;
        }
        m->active = (uint8_t)s;
        return s;
    }
    return -1;
}

int fb_meta_confirm(const fb_flash_t *fl, int slot) {
    fb_boot_meta_t m;
    fb_meta_load(fl, &m);
    if (m.status[slot] != FB_SLOT_TRIAL) return 0;   /* nothing to confirm (already confirmed) */
    m.status[slot] = FB_SLOT_CONFIRMED;
    m.attempts = 0;
    if (m.version[slot] > m.min_version) m.min_version = m.version[slot];
    return fb_meta_store(fl, &m);
}

int fb_meta_mark_new(const fb_flash_t *fl, int slot, uint32_t version) {
    fb_boot_meta_t m;
    fb_meta_load(fl, &m);
    m.status[slot] = FB_SLOT_NEW;
    m.version[slot] = version;
    return fb_meta_store(fl, &m);
}
