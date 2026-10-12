#pragma once
#include <stddef.h>
#include <stdint.h>
/* Test helper: builds a signed image blob (header + body) with a deterministic test key. */
void test_keypair(uint8_t sk[64], uint8_t pk[32]);
size_t test_build_image(uint8_t *out, size_t cap, const uint8_t sk[64], uint32_t version, uint8_t slot,
                        uint8_t node_id, uint16_t hw_id, const uint8_t *body, uint32_t body_len);
