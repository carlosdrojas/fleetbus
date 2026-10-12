#include <string.h>
#include "ota_receiver.h"

void fb_ota_init(fb_ota_rx_t *rx, const fb_flash_t *fl) { memset(rx, 0, sizeof *rx); rx->fl = fl; rx->state = FB_OTA_IDLE; }

int fb_ota_begin(fb_ota_rx_t *rx, int slot, uint16_t total_blocks) {
    if (slot < 0 || slot > 1 || total_blocks == 0 || (uint32_t)total_blocks * FB_OTA_BLOCK_BYTES > FB_SLOT_SIZE) return -1;
    rx->slot = slot; rx->total_blocks = total_blocks; rx->next_block = 0; rx->written = 0; rx->stage_len = 0;
    for (uint32_t a = FB_SLOT_ADDR(slot); a < FB_SLOT_ADDR(slot) + FB_SLOT_SIZE; a += FB_FLASH_PAGE)
        if (rx->fl->erase_page(rx->fl->ctx, a) != 0) { rx->state = FB_OTA_FAILED; return -2; }
    rx->state = FB_OTA_RECEIVING;
    return 0;
}

static int flush_dword(fb_ota_rx_t *rx) {
    if (rx->fl->write(rx->fl->ctx, FB_SLOT_ADDR(rx->slot) + rx->written, rx->stage, 8) != 0) return -1;
    rx->written += 8; rx->stage_len = 0;
    return 0;
}

int fb_ota_block(fb_ota_rx_t *rx, uint16_t block, const uint8_t data[FB_OTA_BLOCK_BYTES], uint16_t *next_out) {
    *next_out = rx->next_block;
    if (rx->state != FB_OTA_RECEIVING) return FB_OTA_ACK_BAD_BLOCK;
    if (block < rx->next_block) return FB_OTA_ACK_OK;          /* duplicate: already have it, re-ACK */
    if (block != rx->next_block || block >= rx->total_blocks) return FB_OTA_ACK_BAD_BLOCK;
    for (unsigned i = 0; i < FB_OTA_BLOCK_BYTES; i++) {
        rx->stage[rx->stage_len++] = data[i];
        if (rx->stage_len == 8 && flush_dword(rx) != 0) { rx->state = FB_OTA_FAILED; return FB_OTA_ACK_FLASH; }
    }
    rx->next_block++;
    if (rx->next_block == rx->total_blocks) {
        if (rx->stage_len) {                                    /* pad the tail with 0xFF and program it */
            memset(rx->stage + rx->stage_len, 0xFF, 8 - rx->stage_len);
            if (flush_dword(rx) != 0) { rx->state = FB_OTA_FAILED; return FB_OTA_ACK_FLASH; }
        }
        rx->state = FB_OTA_DONE;
    }
    *next_out = rx->next_block;
    return FB_OTA_ACK_OK;
}

void fb_ota_abort(fb_ota_rx_t *rx) { rx->state = FB_OTA_IDLE; rx->stage_len = 0; }
