#include <string.h>
#include "image.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int fb_image_verify(const fb_flash_t *fl, int slot, uint8_t node_id, uint32_t min_version,
                    const uint8_t pubkey[32], uint32_t *version_out) {
    uint32_t base = FB_SLOT_ADDR(slot);
    uint8_t h[FB_IMG_OFF_SIG + 64];
    if (fl->read(fl->ctx, base, h, sizeof h) != 0) return FB_IMG_ERR_READ;
    if (rd32(h) != FB_IMG_MAGIC) return FB_IMG_ERR_MAGIC;
    if ((uint16_t)(h[4] | (h[5] << 8)) != FB_IMG_HW_G431) return FB_IMG_ERR_HW;
    if (h[6] != (uint8_t)slot) return FB_IMG_ERR_SLOT;
    if (h[7] != 1) return FB_IMG_ERR_MAGIC;
    if (h[0x10] != node_id) return FB_IMG_ERR_NODE;
    uint32_t version = rd32(h + 8), size = rd32(h + 12);
    if (size == 0 || size > FB_SLOT_SIZE - FB_IMG_HDR_SIZE) return FB_IMG_ERR_SIZE;

    /* Signature first (cheap rejection of forged headers), then hash the body in chunks. */
    if (crypto_ed25519_check(h + FB_IMG_OFF_SIG, pubkey, h, FB_IMG_SIGNED_LEN) != 0) return FB_IMG_ERR_SIG;

    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    uint8_t buf[256];
    for (uint32_t off = 0; off < size; off += sizeof buf) {
        size_t n = size - off < sizeof buf ? size - off : sizeof buf;
        if (fl->read(fl->ctx, base + FB_IMG_HDR_SIZE + off, buf, n) != 0) return FB_IMG_ERR_READ;
        crypto_sha512_update(&ctx, buf, n);
    }
    uint8_t digest[64];
    crypto_sha512_final(&ctx, digest);
    if (crypto_verify64(digest, h + FB_IMG_OFF_SHA) != 0) return FB_IMG_ERR_HASH;

    if (version < min_version) return FB_IMG_ERR_DOWNGRADE;
    if (version_out) *version_out = version;
    return FB_IMG_OK;
}
