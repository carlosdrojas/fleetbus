#pragma once
/* Body-node instrument cluster for a 160x128 RGB565 display (FleetBus Lite option 4, docs/lite-dashboards.md).
 * Hardware-free: it draws only through a fill-rectangle callback, so the same code drives the ST7735 on the board
 * (bsp_display_fill) and a framebuffer in the host tests (firmware/tests/test_gauge.c).
 *
 * Layout: title bar; a 180° pack-voltage gauge (2.8–4.2 V scale with red/amber/green zones at the firmware's fault and
 * warning thresholds) with a needle and the value; temperature, current and state of charge on the right; a state
 * banner at the bottom (NORMAL / WARNING / FAULT / SAFE / BATTERY SILENT / NO DATA / LIMP HOME).
 * The first gv_draw() paints everything; later calls repaint only what changed, which keeps SPI traffic small. */
#include <stdint.h>
#include "fb_types.h"

#define GV_W 160
#define GV_H 128
#define GV_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

typedef struct {
    void (*fill)(void *ctx, int x, int y, int w, int h, uint16_t rgb565);
    void *ctx;
} gv_ops_t;

typedef struct {
    int have;                 /* a BatteryStatus has been received at all */
    int32_t pack_mv, ma, temp_dc;
    uint8_t soc_pct;
    int battery_alive;        /* heartbeat seen within the firmware peer timeout */
    fb_state_t battery_state; /* from the battery heartbeat */
    int limp_home;            /* the body node's own limp-home output */
} gv_inputs_t;

typedef struct {
    gv_ops_t ops;
    int drawn;                /* static parts painted */
    int needle_deg;           /* last needle angle drawn, -1 = none */
    char volt_txt[8], temp_txt[8], curr_txt[8], soc_txt[8];
    int banner;               /* last banner id drawn, -1 = none */
} gv_t;

void gv_init(gv_t *g, gv_ops_t ops);
void gv_draw(gv_t *g, const gv_inputs_t *in);
/* Needle angle for a pack voltage: 0° at 2.8 V (left), 180° at 4.2 V (right), clamped. Exposed for tests. */
int  gv_volt_to_deg(int32_t pack_mv);
