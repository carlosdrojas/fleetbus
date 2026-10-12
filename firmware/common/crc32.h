#pragma once
#include <stddef.h>
#include <stdint.h>
/* CRC-32C (Castagnoli), reflected, poly 0x82F63B78, init/xorout 0xFFFFFFFF. Check value of "123456789" is 0xE3069283. */
uint32_t fb_crc32c(const uint8_t *data, size_t len);
uint32_t fb_crc32c_update(uint32_t crc, const uint8_t *data, size_t len); /* crc = 0 to start; chainable */
