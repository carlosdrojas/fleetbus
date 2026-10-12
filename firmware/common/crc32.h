#pragma once
#include <stddef.h>
#include <stdint.h>
/* CRC-32C (Castagnoli), reflected, poly 0x82F63B78. MOCK(M5): bitwise version; M5 adds a table-driven one shared with the golden vectors. */
uint32_t fb_crc32c(const uint8_t *data, size_t len);
