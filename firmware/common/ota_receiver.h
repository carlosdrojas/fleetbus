#pragma once
#include <stdint.h>
#include "flash_if.h"

/* Receives an image over CAN as 6-byte blocks and programs it into the inactive slot.
 * Blocks must arrive in order; a duplicate is re-ACKed, a gap is rejected with the expected block so the
 * sender resumes from there. The receiver does not verify the image: the caller runs fb_image_verify()
 * once FB_OTA_DONE is reported, before marking the slot NEW. */
#define FB_OTA_BLOCK_BYTES 6u

typedef enum { FB_OTA_IDLE = 0, FB_OTA_RECEIVING = 1, FB_OTA_DONE = 2, FB_OTA_FAILED = 3 } fb_ota_state_t;
typedef enum { FB_OTA_ACK_OK = 0, FB_OTA_ACK_BAD_BLOCK = 1, FB_OTA_ACK_FLASH = 4 } fb_ota_ack_t;

typedef struct {
    const fb_flash_t *fl;
    fb_ota_state_t state;
    int slot;
    uint16_t next_block, total_blocks;
    uint32_t written;            /* bytes programmed so far */
    uint8_t stage[8];            /* partial double-word */
    uint8_t stage_len;
} fb_ota_rx_t;

void fb_ota_init(fb_ota_rx_t *rx, const fb_flash_t *fl);
/* Starts a transfer into `slot` of `total_blocks` blocks (<= slot capacity). */
int  fb_ota_begin(fb_ota_rx_t *rx, int slot, uint16_t total_blocks);
/* Feeds a block. Returns an fb_ota_ack_t; *next_out is the next block the sender should send. */
int  fb_ota_block(fb_ota_rx_t *rx, uint16_t block, const uint8_t data[FB_OTA_BLOCK_BYTES], uint16_t *next_out);
void fb_ota_abort(fb_ota_rx_t *rx);
