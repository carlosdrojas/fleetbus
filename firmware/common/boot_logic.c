#include "boot_logic.h"
#include "boot_meta.h"
#include "image.h"

typedef struct { const fb_flash_t *fl; uint8_t node; const uint8_t *pk; const fb_boot_meta_t *m; uint32_t version[2]; } ctx_t;

static int image_ok(void *c, int slot) {
    ctx_t *x = c;
    /* Anti-downgrade floor applies to freshly installed images. A slot that was already accepted (TRIAL/CONFIRMED)
     * may still be booted as the rollback target even if a newer image has since been confirmed. */
    uint32_t floor = x->m->status[slot] == FB_SLOT_NEW ? x->m->min_version : 0;
    return fb_image_verify(x->fl, slot, x->node, floor, x->pk, &x->version[slot]) == FB_IMG_OK;
}

int fb_boot_run(const fb_flash_t *fl, uint8_t node_id, const uint8_t pubkey[32], fb_boot_result_t *r) {
    fb_boot_meta_t m, before;
    fb_meta_load(fl, &m);
    before = m;
    ctx_t ctx = { fl, node_id, pubkey, &before, { 0, 0 } };
    int slot = fb_boot_decide(&m, image_ok, &ctx);
    r->rolled_back = 0;
    for (int s = 0; s < 2; s++) if (m.status[s] == FB_SLOT_BAD && before.status[s] != FB_SLOT_BAD) r->rolled_back = 1;
    if (slot < 0) { fb_meta_store(fl, &m); return -1; }
    if (fb_meta_store(fl, &m) != 0) return -1;     /* cannot persist the attempt counter: do not boot untracked */
    r->slot = slot;
    r->trial = m.status[slot] == FB_SLOT_TRIAL;
    r->version = ctx.version[slot];   /* from the one verify that accepted it: a second verify doubled boot time (P0) */
    return 0;
}
