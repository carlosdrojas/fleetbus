#pragma once
#include <stdint.h>
/* MOCK(M9): boot metadata and A/B slot selection. Declared only; no implementation until M9. */
typedef enum { FB_SLOT_A, FB_SLOT_B } fb_slot_t;
typedef enum { FB_SLOT_EMPTY, FB_SLOT_NEW, FB_SLOT_TRIAL, FB_SLOT_CONFIRMED, FB_SLOT_BAD } fb_slot_status_t;
fb_slot_t fb_boot_select(const fb_slot_status_t status[2], uint32_t boot_attempts);
