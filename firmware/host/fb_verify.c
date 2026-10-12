/* Host CLI: run the firmware's own fb_image_verify() on a signed image file.
 *   fb_verify <pubkey_hex64> <image.signed> <node_id> [min_version]
 * Exit code = result (0 ok, otherwise -fb_img_result_t). Used by tests to prove tools/sign_image.py and the C verifier agree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
#include "../tests/fake_flash.h"

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: fb_verify <pubkey_hex> <image> <node_id> [min_version]\n"); return 99; }
    uint8_t pk[32];
    if (strlen(argv[1]) != 64) return 98;
    for (int i = 0; i < 32; i++) { unsigned v; sscanf(argv[1] + 2 * i, "%2x", &v); pk[i] = (uint8_t)v; }
    FILE *f = fopen(argv[2], "rb"); if (!f) return 97;
    static uint8_t buf[FB_SLOT_SIZE + 4096]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    if (n < 0x70 || n > FB_SLOT_SIZE) { printf("size out of range\n"); return 96; }
    static fake_flash_t ff; fb_flash_t fl; fake_flash_init(&ff, &fl);
    int slot = buf[6] & 1;                                  /* the header's own slot field decides where it is placed */
    memcpy(ff.mem + (FB_SLOT_ADDR(slot) - FB_FLASH_BASE), buf, n);
    uint32_t ver = 0;
    int r = fb_image_verify(&fl, slot, (uint8_t)atoi(argv[3]), argc > 4 ? (uint32_t)atoi(argv[4]) : 0, pk, &ver);
    printf("result=%d version=%u slot=%d\n", r, ver, slot);
    return r == 0 ? 0 : -r;
}
