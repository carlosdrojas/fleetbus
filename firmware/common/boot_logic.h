#pragma once
#include <stdint.h>
#include "flash_if.h"

/* What the bootloader does on every reset, minus jumping: load metadata, verify candidate slots, pick one, count the
 * attempt, persist. Shared by the real bootloader (firmware/bootloader) and the host simulator. */
typedef struct {
    int slot;                /* slot to boot */
    uint32_t version;        /* its image version */
    int trial;               /* 1 if it must be confirmed by the app */
    int rolled_back;         /* a slot was marked BAD during this decision */
} fb_boot_result_t;

/* Returns 0 on success (*r filled) or -1 if no bootable slot (stay in bootloader). */
int fb_boot_run(const fb_flash_t *fl, uint8_t node_id, const uint8_t pubkey[32], fb_boot_result_t *r);
