#pragma once
#include <stdint.h>
/* MOCK(M11): block-transfer receiver writing the inactive slot; resumable. Declared only. */
typedef struct { uint16_t next_block, total_blocks; } fb_ota_rx_t;
int fb_ota_rx_block(fb_ota_rx_t *rx, uint16_t block, const uint8_t data[8]);
