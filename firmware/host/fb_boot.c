/* Host CLI: run the bootloader's own decision (fb_boot_run) on a flash image assembled from files.
 *   fb_boot <pubkey_hex64> <node_id> <meta_addr_hex> <meta.bin> [<image.signed> ...]
 * Each image is placed in the slot its header names. Prints the decision and the metadata the bootloader stored.
 * Exit 0 if a slot was chosen. Used by tests to prove tools/provision_meta.py + tools/sign_image.py produce a flash
 * the C bootloader logic boots, for each board's flash map. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot_logic.h"
#include "boot_meta.h"
#include "image.h"
#include "../tests/fake_flash.h"

static size_t load(const char *path, uint8_t *buf, size_t cap) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(97); }
    size_t n = fread(buf, 1, cap, f); fclose(f); return n;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: fb_boot <pubkey_hex> <node_id> <meta_addr_hex> <meta.bin> [image ...]\n"); return 99; }
    uint8_t pk[32];
    if (strlen(argv[1]) != 64) return 98;
    for (int i = 0; i < 32; i++) { unsigned v; sscanf(argv[1] + 2 * i, "%2x", &v); pk[i] = (uint8_t)v; }
    static fake_flash_t ff; fb_flash_t fl; fake_flash_init(&ff, &fl);
    static uint8_t buf[FB_SLOT_SIZE + 4096];
    uint32_t meta_addr = (uint32_t)strtoul(argv[3], 0, 16);
    if (meta_addr != FB_META_A_ADDR) { printf("meta address 0x%08x is not this board's 0x%08x\n", meta_addr, FB_META_A_ADDR); return 96; }
    size_t n = load(argv[4], buf, sizeof buf);
    memcpy(ff.mem + (meta_addr - FB_FLASH_BASE), buf, n);
    for (int i = 5; i < argc; i++) {
        n = load(argv[i], buf, sizeof buf);
        memcpy(ff.mem + (FB_SLOT_ADDR(buf[6] & 1) - FB_FLASH_BASE), buf, n);
    }
    fb_boot_result_t r;
    int rc = fb_boot_run(&fl, (uint8_t)atoi(argv[2]), pk, &r);
    fb_boot_meta_t m; fb_meta_load(&fl, &m);
    printf("board=%s rc=%d slot=%d trial=%d version=%u status=%u,%u attempts=%u seq=%u\n", FB_BOARD_NAME, rc,
           rc == 0 ? r.slot : -1, rc == 0 ? r.trial : 0, rc == 0 ? r.version : 0, m.status[0], m.status[1], m.attempts, m.seq);
    return rc == 0 ? 0 : 1;
}
