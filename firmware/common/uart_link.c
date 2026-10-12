#include <string.h>
#include "uart_link.h"

uint8_t fb_crc8(const uint8_t *d, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : (c << 1));
    }
    return c;
}

static size_t put(uint8_t *out, size_t n, uint8_t b) {
    if (b == FB_LINK_SOF || b == FB_LINK_ESC) { out[n++] = FB_LINK_ESC; out[n++] = b ^ 0x20; }
    else out[n++] = b;
    return n;
}

size_t fb_link_encode(uint32_t id, const uint8_t *data, uint8_t dlc, uint8_t *out) {
    uint8_t body[12]; size_t m = 0, n = 0;
    if (dlc > 8) dlc = 8;
    body[m++] = (uint8_t)(id & 0xFF); body[m++] = (uint8_t)((id >> 8) & 0x07); body[m++] = dlc;
    memcpy(body + m, data, dlc); m += dlc;
    body[m] = fb_crc8(body, m); m++;
    out[n++] = FB_LINK_SOF;
    for (size_t i = 0; i < m; i++) n = put(out, n, body[i]);
    return n;
}

void fb_link_rx_init(fb_link_rx_t *rx) { memset(rx, 0, sizeof *rx); }

int fb_link_feed(fb_link_rx_t *rx, uint8_t b, fb_can_frame_t *out) {
    if (b == FB_LINK_SOF) {                                  /* SOF always resynchronises: it is never part of a body */
        int complete = 0;
        if (rx->in_frame && rx->n >= 4) {
            uint8_t dlc = rx->buf[2];
            if (dlc <= 8 && rx->n == (uint8_t)(4 + dlc) && fb_crc8(rx->buf, rx->n - 1u) == rx->buf[rx->n - 1u]) {
                out->id = (uint32_t)rx->buf[0] | ((uint32_t)(rx->buf[1] & 0x07) << 8); out->dlc = dlc; memcpy(out->data, rx->buf + 3, dlc);
                complete = 1;
            } else rx->crc_errors++;
        } else if (rx->in_frame && rx->n > 0) rx->framing_errors++;
        rx->n = 0; rx->esc = 0; rx->in_frame = 1;
        return complete;
    }
    if (!rx->in_frame) return 0;
    if (b == FB_LINK_ESC) { rx->esc = 1; return 0; }
    if (rx->esc) { b ^= 0x20; rx->esc = 0; }
    if (rx->n >= sizeof rx->buf) { rx->framing_errors++; rx->in_frame = 0; rx->n = 0; return 0; }
    rx->buf[rx->n++] = b;
    return 0;
}
