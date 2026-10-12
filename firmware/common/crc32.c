#include "crc32.h"
/* MOCK(M5): slow bitwise implementation. */
uint32_t fb_crc32c(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
    }
    return ~crc;
}
