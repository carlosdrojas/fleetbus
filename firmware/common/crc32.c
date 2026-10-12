#include "crc32.h"

static uint32_t table[256];
static int table_ready;

static void build_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1u)));
        table[i] = c;
    }
    table_ready = 1;
}

uint32_t fb_crc32c_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!table_ready) build_table();
    crc = ~crc;
    for (size_t i = 0; i < len; i++) crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFFu];
    return ~crc;
}

uint32_t fb_crc32c(const uint8_t *data, size_t len) { return fb_crc32c_update(0, data, len); }
