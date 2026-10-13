#include <string.h>
#include "gauge_view.h"

/* Geometry (160x128 landscape). */
#define CX 58                 /* gauge centre */
#define CY 84
#define R_ZONE 42             /* zone dots */
#define R_NEEDLE 34
#define TXT_X 110             /* right column */
#define BANNER_Y 108
#define SCALE_LO_MV 2800
#define SCALE_HI_MV 4200
#define FAULT_MV 3000         /* firmware/common/fault_fsm.c defaults */
#define WARN_MV 3300

#define C_BG     0x0000u
#define C_WHITE  0xFFFFu
#define C_TITLE  GV_RGB565(31, 95, 168)
#define C_LABEL  GV_RGB565(159, 216, 255)
#define C_RED    GV_RGB565(226, 75, 74)
#define C_AMBER  GV_RGB565(231, 160, 32)
#define C_GREEN  GV_RGB565(44, 154, 79)
#define C_GREY   GV_RGB565(90, 90, 90)

/* sin(0..90°) in Q12. */
static const int16_t SIN_Q12[91] = {
    0, 71, 143, 214, 286, 357, 428, 499, 570, 641, 711, 782, 852,
    921, 991, 1060, 1129, 1198, 1266, 1334, 1401, 1468, 1534, 1600, 1666, 1731,
    1796, 1860, 1923, 1986, 2048, 2110, 2171, 2231, 2290, 2349, 2408, 2465, 2522,
    2578, 2633, 2687, 2741, 2793, 2845, 2896, 2946, 2996, 3044, 3091, 3138, 3183,
    3228, 3271, 3314, 3355, 3396, 3435, 3474, 3511, 3547, 3582, 3617, 3650, 3681,
    3712, 3742, 3770, 3798, 3824, 3849, 3873, 3896, 3917, 3937, 3956, 3974, 3991,
    4006, 4021, 4034, 4046, 4056, 4065, 4074, 4080, 4086, 4090, 4094, 4095, 4096 };

static int sin_q12(int deg) { return deg <= 90 ? SIN_Q12[deg] : SIN_Q12[180 - deg]; }
static int cos_q12(int deg) { return deg <= 90 ? SIN_Q12[90 - deg] : -SIN_Q12[deg - 90]; }
static int q12_mul(int v, int r) { int p = v * r; return p >= 0 ? (p + 2048) / 4096 : -((-p + 2048) / 4096); }
/* Angle 0° points left, 90° up, 180° right (upper half circle). */
static int pt_x(int deg, int r) { return CX - q12_mul(cos_q12(deg), r); }
static int pt_y(int deg, int r) { return CY - q12_mul(sin_q12(deg), r); }

/* 5x7 glyphs, column-major, bit 0 = top row. Only the characters the cluster prints. */
static const char GLYPH_CHARS[] = " %-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const uint8_t GLYPHS[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x08, 0x08, 0x08, 0x08, 0x08}, /* sp % - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},                                   /* . / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},   /* 0 1 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},   /* 3 4 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},   /* 6 7 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},                                   /* 9 : */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},   /* A B C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},   /* D E F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},   /* G H I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},   /* J K L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},   /* M N O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},   /* P Q R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},   /* S T U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},   /* V W X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43} };                                 /* Y Z */

static void fill(const gv_t *g, int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GV_W) w = GV_W - x;
    if (y + h > GV_H) h = GV_H - y;
    if (w > 0 && h > 0) g->ops.fill(g->ops.ctx, x, y, w, h, c);
}

/* Text: background box first (one call), then only the lit pixels. Advance 6*scale per character. */
static int text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }
static void text(const gv_t *g, int x, int y, const char *s, int scale, uint16_t fg) {
    for (int i = 0; s[i]; i++) {
        const char *p = strchr(GLYPH_CHARS, s[i]);
        if (!p || !s[i]) continue;
        const uint8_t *col = GLYPHS[p - GLYPH_CHARS];
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (col[cx] & (1u << cy)) fill(g, x + (i * 6 + cx) * scale, y + cy * scale, scale, scale, fg);
    }
}
static void text_field(const gv_t *g, int x, int y, int box_w, const char *s, int scale, uint16_t fg) {
    fill(g, x, y, box_w, 7 * scale, C_BG);
    text(g, x, y, s, scale, fg);
}

int gv_volt_to_deg(int32_t mv) {
    if (mv <= SCALE_LO_MV) return 0;
    if (mv >= SCALE_HI_MV) return 180;
    return (int)((mv - SCALE_LO_MV) * 180 / (SCALE_HI_MV - SCALE_LO_MV));
}

static void needle(const gv_t *g, int deg, uint16_t c) {   /* Bresenham, 2 px thick */
    int x0 = CX, y0 = CY, x1 = pt_x(deg, R_NEEDLE), y1 = pt_y(deg, R_NEEDLE);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fill(g, x0, y0, 2, 2, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fmt_fixed(char *out, int32_t v, int div, int decimals, char unit) {   /* v/div with N decimals, no printf */
    int n = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    int32_t whole = v / div, frac = v % div;
    if (whole > 99) whole = 99;
    if (whole >= 10) out[n++] = (char)('0' + whole / 10);
    out[n++] = (char)('0' + whole % 10);
    out[n++] = '.';
    int32_t d = div;
    for (int i = 0; i < decimals; i++) { d /= 10; out[n++] = (char)('0' + (frac / (d ? d : 1)) % 10); }
    out[n++] = unit; out[n] = 0;
}

static void draw_static(gv_t *g) {
    fill(g, 0, 0, GV_W, GV_H, C_BG);
    fill(g, 0, 0, GV_W, 12, C_TITLE);
    text(g, 3, 3, "FLEETBUS  BODY NODE", 1, C_WHITE);
    for (int deg = 0; deg <= 180; deg += 3) {
        int32_t mv = SCALE_LO_MV + (int32_t)deg * (SCALE_HI_MV - SCALE_LO_MV) / 180;
        uint16_t c = mv < FAULT_MV ? C_RED : mv < WARN_MV ? C_AMBER : C_GREEN;
        fill(g, pt_x(deg, R_ZONE) - 2, pt_y(deg, R_ZONE) - 2, 4, 4, c);
    }
    text(g, TXT_X, 22, "TEMP", 1, C_LABEL);
    text(g, TXT_X, 50, "CURR", 1, C_LABEL);
    text(g, TXT_X, 78, "SOC", 1, C_LABEL);
}

enum { B_NODATA, B_NORMAL, B_WARNING, B_FAULT, B_SAFE, B_SILENT, B_LIMP };
static const char *const BANNER_TXT[] = { "NO DATA", "NORMAL", "BATTERY WARNING", "BATTERY FAULT", "BATTERY SAFE",
                                          "BATTERY SILENT", "LIMP HOME" };
static const uint16_t BANNER_COL[] = { C_GREY, C_GREEN, C_AMBER, C_RED, C_RED, C_RED, C_AMBER };

static int banner_for(const gv_inputs_t *in) {
    if (!in->have && !in->battery_alive) return B_NODATA;
    if (!in->battery_alive) return B_SILENT;
    if (in->battery_state == FB_ST_FAULT) return B_FAULT;
    if (in->battery_state == FB_ST_SAFE) return B_SAFE;
    if (in->battery_state == FB_ST_WARNING) return B_WARNING;
    if (in->limp_home) return B_LIMP;
    return B_NORMAL;
}

static void field(gv_t *g, char *last, const char *now, int x, int y, int box_w, int scale) {
    if (strcmp(last, now) == 0) return;
    text_field(g, x, y, box_w, now, scale, C_WHITE);
    strcpy(last, now);
}

void gv_init(gv_t *g, gv_ops_t ops) {
    memset(g, 0, sizeof *g);
    g->ops = ops;
    g->needle_deg = -1;
    g->banner = -1;
}

void gv_draw(gv_t *g, const gv_inputs_t *in) {
    if (!g->drawn) { draw_static(g); g->drawn = 1; }

    int deg = in->have ? gv_volt_to_deg(in->pack_mv) : -1;
    if (deg != g->needle_deg) {
        if (g->needle_deg >= 0) needle(g, g->needle_deg, C_BG);
        if (deg >= 0) needle(g, deg, C_WHITE);
        g->needle_deg = deg;
    }
    fill(g, CX - 3, CY - 3, 7, 7, C_WHITE);                 /* hub (also covers the needles' shared start) */

    char v[8], t[8], a[8], s[8];
    if (in->have) {
        fmt_fixed(v, in->pack_mv, 1000, 2, 'V');
        fmt_fixed(t, in->temp_dc, 10, 1, 'C');
        fmt_fixed(a, in->ma, 1000, 2, 'A');
        uint8_t soc = in->soc_pct > 100 ? 100 : in->soc_pct;
        int n = 0;
        if (soc >= 100) s[n++] = '1';
        if (soc >= 10) s[n++] = (char)('0' + (soc / 10) % 10);
        s[n++] = (char)('0' + soc % 10); s[n++] = '%'; s[n] = 0;
    } else {
        strcpy(v, "-.--V"); strcpy(t, "--.-C"); strcpy(a, "-.--A"); strcpy(s, "--%");
    }
    field(g, g->volt_txt, v, CX - 36, CY + 8, 72, 2);
    field(g, g->temp_txt, t, TXT_X, 33, GV_W - TXT_X, 1);
    field(g, g->curr_txt, a, TXT_X, 61, GV_W - TXT_X, 1);
    field(g, g->soc_txt, s, TXT_X, 89, GV_W - TXT_X, 1);

    int b = banner_for(in);
    if (b != g->banner) {
        fill(g, 0, BANNER_Y, GV_W, GV_H - BANNER_Y, BANNER_COL[b]);
        const char *bt = BANNER_TXT[b];
        text(g, (GV_W - text_width(bt, 1)) / 2, BANNER_Y + 6, bt, 1, b == B_NODATA ? C_WHITE : C_BG);
        g->banner = b;
    }
}
