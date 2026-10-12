#include <string.h>
#include "image_builder.h"
#include "image.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

void test_keypair(uint8_t sk[64], uint8_t pk[32]) {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t)(i * 7 + 1);
    crypto_ed25519_key_pair(sk, pk, seed);
}

size_t test_build_image(uint8_t *out, size_t cap, const uint8_t sk[64], uint32_t version, uint8_t slot,
                        uint8_t node_id, uint16_t hw_id, const uint8_t *body, uint32_t body_len) {
    if (FB_IMG_HDR_SIZE + body_len > cap) return 0;
    memset(out, 0xFF, FB_IMG_HDR_SIZE);
    uint8_t *h = out;
    memset(h, 0, 0x20);
    uint32_t magic = FB_IMG_MAGIC;
    memcpy(h, &magic, 4); h[4] = hw_id & 0xFF; h[5] = hw_id >> 8; h[6] = slot; h[7] = 1;
    memcpy(h + 8, &version, 4); memcpy(h + 12, &body_len, 4); h[0x10] = node_id;
    crypto_sha512(h + FB_IMG_OFF_SHA, body, body_len);
    crypto_ed25519_sign(h + FB_IMG_OFF_SIG, sk, h, FB_IMG_SIGNED_LEN);
    memcpy(out + FB_IMG_HDR_SIZE, body, body_len);
    return FB_IMG_HDR_SIZE + body_len;
}
