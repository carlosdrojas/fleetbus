#pragma once
#include <stdint.h>
#include "flash_if.h"

/* Boot metadata and A/B slot logic. Two metadata copies in two flash pages; the valid copy with the highest
 * sequence number wins, and a new copy is always written to the OLDER page, so a power cut during an update
 * leaves the previous copy intact. */
typedef enum { FB_SLOT_EMPTY = 0, FB_SLOT_NEW = 1, FB_SLOT_TRIAL = 2, FB_SLOT_CONFIRMED = 3, FB_SLOT_BAD = 4 } fb_slot_status_t;

#define FB_BOOT_MAX_ATTEMPTS 3u

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t min_version;     /* anti-downgrade: highest CONFIRMED version so far */
    uint32_t version[2];      /* version installed in each slot */
    uint8_t  status[2];
    uint8_t  attempts;        /* boots of the current TRIAL slot */
    uint8_t  active;          /* slot to prefer */
    uint32_t reserved;
    uint32_t crc;             /* CRC-32C over all previous bytes */
} fb_boot_meta_t;             /* 36 bytes, stored padded to 40 */

#define FB_META_MAGIC 0x4D455441u /* "META" */

void fb_meta_default(fb_boot_meta_t *m);
/* Loads the best valid copy. Returns 0 and fills *m; if neither copy is valid returns -1 and *m = default. */
int  fb_meta_load(const fb_flash_t *fl, fb_boot_meta_t *m);
/* Stores m (seq is incremented here) to the older page. Returns 0 on success, negative on flash error. */
int  fb_meta_store(const fb_flash_t *fl, fb_boot_meta_t *m);

/* Boot decision. Mutates *m (attempt counting, marking BAD, NEW->TRIAL). Returns the slot (0/1) to boot,
 * or -1 if nothing bootable (stay in bootloader). `image_ok(slot)` is the signature/hash/anti-downgrade check. */
int  fb_boot_decide(fb_boot_meta_t *m, int (*image_ok)(void *ctx, int slot), void *ctx);

/* Application side: mark the running TRIAL slot confirmed and raise the anti-downgrade floor. */
int  fb_meta_confirm(const fb_flash_t *fl, int slot);
/* OTA side: slot has been fully written and verified; mark NEW with its version. */
int  fb_meta_mark_new(const fb_flash_t *fl, int slot, uint32_t version);
