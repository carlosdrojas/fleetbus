#pragma once
#include <stddef.h>
#include <stdint.h>
#include "flash_if.h"

/* Signed firmware image: 512-byte header at the start of a slot, then the application (vector table at +0x200,
 * which satisfies the Cortex-M4 VTOR alignment for the G4's vector count).
 * The Ed25519 signature covers header bytes [0, 0x50) i.e. everything before the signature field, which
 * includes the SHA-512 of the application body. */
#define FB_IMG_MAGIC      0x4D494246u  /* "FBIM" little-endian */
#define FB_IMG_HDR_SIZE   0x200u
#define FB_IMG_HW_G431    0x4301u

/* On-flash layout (little-endian):
 *   0x00 magic u32 | 0x04 hw_id u16 | 0x06 slot u8 | 0x07 hdr_version u8 | 0x08 version u32 | 0x0C size u32
 *   0x10 node_id u8 | 0x11..0x1F zero | 0x20 sha512[64] | 0x60 signature[64] | rest 0xFF
 * Signed range: [0x00, 0x60). */
#define FB_IMG_SIGNED_LEN 0x60u
#define FB_IMG_OFF_SHA    0x20u
#define FB_IMG_OFF_SIG    0x60u

typedef enum {
    FB_IMG_OK = 0, FB_IMG_ERR_READ = -1, FB_IMG_ERR_MAGIC = -2, FB_IMG_ERR_HW = -3, FB_IMG_ERR_SLOT = -4,
    FB_IMG_ERR_SIZE = -5, FB_IMG_ERR_HASH = -6, FB_IMG_ERR_SIG = -7, FB_IMG_ERR_DOWNGRADE = -8, FB_IMG_ERR_NODE = -9
} fb_img_result_t;

/* Verifies the image in `slot`: magic, hw id, slot, node, size bound, SHA-512 over the body, Ed25519 signature,
 * and version >= min_version (anti-downgrade). Fail-closed: any problem rejects. */
int fb_image_verify(const fb_flash_t *fl, int slot, uint8_t node_id, uint32_t min_version,
                    const uint8_t pubkey[32], uint32_t *version_out);
