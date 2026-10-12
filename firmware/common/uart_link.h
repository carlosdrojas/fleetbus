#pragma once
/* Framing for the telematics node <-> gateway (Raspberry Pi) serial link: CAN frames over a UART/USB-CDC byte stream.
 *   0x7E | id_lo id_hi dlc data[dlc] crc8 | (0x7E and 0x7D inside the body are escaped: 0x7D, byte ^ 0x20)
 * CRC-8 (poly 0x07, init 0) over id_lo..data. Same format on both ends: gateway/fleetbus/uartlink.py. */
#include <stddef.h>
#include <stdint.h>
#include "node_core.h"   /* fb_can_frame_t */

#define FB_LINK_SOF 0x7Eu
#define FB_LINK_ESC 0x7Du
#define FB_LINK_MAX_ENCODED (2u + 2u * (3u + 8u + 1u))

uint8_t fb_crc8(const uint8_t *d, size_t n);
size_t fb_link_encode(uint32_t id, const uint8_t *data, uint8_t dlc, uint8_t *out);   /* returns bytes written */

typedef struct { uint8_t buf[16]; uint8_t n; uint8_t esc; uint8_t in_frame; uint32_t crc_errors, framing_errors; } fb_link_rx_t;
void fb_link_rx_init(fb_link_rx_t *rx);
int  fb_link_feed(fb_link_rx_t *rx, uint8_t byte, fb_can_frame_t *out);   /* 1 when `out` holds a complete valid frame */
