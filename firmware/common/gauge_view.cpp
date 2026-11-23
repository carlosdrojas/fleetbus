#include "gauge_view.hpp"
#include <cstring>

namespace fleetbus {
namespace {

// Geometry (160x128 landscape).
constexpr int CX = 58, CY = 84;          // gauge centre
constexpr int R_ZONE = 42, R_NEEDLE = 34;
constexpr int TXT_X = 110;               // right column
constexpr int BANNER_Y = 108;
constexpr int32_t SCALE_LO_MV = 2800, SCALE_HI_MV = 4200;
constexpr int32_t FAULT_MV = 3000, WARN_MV = 3300;   // BatteryFaultFsm::Config defaults

constexpr uint16_t C_BG = 0x0000u, C_WHITE = 0xFFFFu;
constexpr uint16_t C_TITLE = rgb565(31, 95, 168), C_LABEL = rgb565(159, 216, 255);
constexpr uint16_t C_RED = rgb565(226, 75, 74), C_AMBER = rgb565(231, 160, 32);
constexpr uint16_t C_GREEN = rgb565(44, 154, 79), C_GREY = rgb565(90, 90, 90);

// sin(0..90 deg) in Q12 fixed point: the M0+ has no FPU, and 91 entries cover the half circle by symmetry.
constexpr int16_t SIN_Q12[91] = {
    0, 71, 143, 214, 286, 357, 428, 499, 570, 641, 711, 782, 852,
    921, 991, 1060, 1129, 1198, 1266, 1334, 1401, 1468, 1534, 1600, 1666, 1731,
    1796, 1860, 1923, 1986, 2048, 2110, 2171, 2231, 2290, 2349, 2408, 2465, 2522,
    2578, 2633, 2687, 2741, 2793, 2845, 2896, 2946, 2996, 3044, 3091, 3138, 3183,
    3228, 3271, 3314, 3355, 3396, 3435, 3474, 3511, 3547, 3582, 3617, 3650, 3681,
    3712, 3742, 3770, 3798, 3824, 3849, 3873, 3896, 3917, 3937, 3956, 3974, 3991,
    4006, 4021, 4034, 4046, 4056, 4065, 4074, 4080, 4086, 4090, 4094, 4095, 4096 };

constexpr int sin_q12(int deg) { return deg <= 90 ? SIN_Q12[deg] : SIN_Q12[180 - deg]; }
constexpr int cos_q12(int deg) { return deg <= 90 ? SIN_Q12[90 - deg] : -SIN_Q12[deg - 90]; }
constexpr int q12_mul(int v, int r) { const int p = v * r; return p >= 0 ? (p + 2048) / 4096 : -((-p + 2048) / 4096); }
// Angle 0 deg points left, 90 up, 180 right (upper half circle).
constexpr int pt_x(int deg, int r) { return CX - q12_mul(cos_q12(deg), r); }
constexpr int pt_y(int deg, int r) { return CY - q12_mul(sin_q12(deg), r); }
static_assert(pt_x(0, 10) == CX - 10 && pt_y(90, 10) == CY - 10 && pt_x(180, 10) == CX + 10, "gauge geometry");

// 5x7 glyphs, column-major, bit 0 = top row. Only the characters the cluster prints.
constexpr char GLYPH_CHARS[] = " %-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr uint8_t GLYPHS[][5] = {
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
static_assert(sizeof GLYPHS / sizeof GLYPHS[0] == sizeof GLYPH_CHARS - 1, "one glyph per character");

int text_width(const char* s, int scale) { return static_cast<int>(std::strlen(s)) * 6 * scale - scale; }

// v/div with N decimals plus a unit letter, no printf (newlib-nano printf costs kilobytes of flash).
void fmt_fixed(std::array<char, 8>& out, int32_t v, int32_t div, int decimals, char unit) {
    std::size_t n = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    int32_t whole = v / div;
    const int32_t frac = v % div;
    if (whole > 99) whole = 99;
    if (whole >= 10) out[n++] = static_cast<char>('0' + whole / 10);
    out[n++] = static_cast<char>('0' + whole % 10);
    out[n++] = '.';
    int32_t d = div;
    for (int i = 0; i < decimals; i++) { d /= 10; out[n++] = static_cast<char>('0' + (frac / (d ? d : 1)) % 10); }
    out[n++] = unit;
    out[n] = 0;
}

void set_text(std::array<char, 8>& out, const char* s) { std::strncpy(out.data(), s, out.size() - 1); out.back() = 0; }

enum Banner { B_NODATA, B_NORMAL, B_WARNING, B_FAULT, B_SAFE, B_SILENT, B_LIMP, B_COUNT };
constexpr const char* BANNER_TXT[B_COUNT] = { "NO DATA", "NORMAL", "BATTERY WARNING", "BATTERY FAULT", "BATTERY SAFE",
                                              "BATTERY SILENT", "LIMP HOME" };
constexpr uint16_t BANNER_COL[B_COUNT] = { C_GREY, C_GREEN, C_AMBER, C_RED, C_RED, C_RED, C_AMBER };

Banner banner_for(const GaugeInputs& in) {
    if (!in.have && !in.battery_alive) return B_NODATA;
    if (!in.battery_alive) return B_SILENT;
    if (in.battery_state == FB_ST_FAULT) return B_FAULT;
    if (in.battery_state == FB_ST_SAFE) return B_SAFE;
    if (in.battery_state == FB_ST_WARNING) return B_WARNING;
    if (in.limp_home) return B_LIMP;
    return B_NORMAL;
}

}  // namespace

// Clip to the screen, then forward. Every drawing path goes through here, so nothing can write outside the panel.
void GaugeView::fill(int x, int y, int w, int h, uint16_t c) noexcept {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kLcdW) w = kLcdW - x;
    if (y + h > kLcdH) h = kLcdH - y;
    if (w > 0 && h > 0) canvas_.fill(x, y, w, h, c);
}

// Only the lit pixels are drawn; callers clear the box first. Advance 6*scale per character.
void GaugeView::text(int x, int y, const char* s, int scale, uint16_t fg) noexcept {
    for (int i = 0; s[i]; i++) {
        const char* p = std::strchr(GLYPH_CHARS, s[i]);
        if (!p) continue;
        const uint8_t* col = GLYPHS[p - GLYPH_CHARS];
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (col[cx] & (1u << cy)) fill(x + (i * 6 + cx) * scale, y + cy * scale, scale, scale, fg);
    }
}

void GaugeView::field(Text& last, const Text& now, int x, int y, int box_w, int scale) noexcept {
    if (std::strcmp(last.data(), now.data()) == 0) return;        // unchanged: no SPI traffic
    fill(x, y, box_w, 7 * scale, C_BG);
    text(x, y, now.data(), scale, C_WHITE);
    last = now;
}

int GaugeView::volt_to_deg(int32_t mv) noexcept {
    if (mv <= SCALE_LO_MV) return 0;
    if (mv >= SCALE_HI_MV) return 180;
    return static_cast<int>((mv - SCALE_LO_MV) * 180 / (SCALE_HI_MV - SCALE_LO_MV));
}

void GaugeView::needle(int deg, uint16_t c) noexcept {   // Bresenham, 2 px thick
    int x0 = CX, y0 = CY;
    const int x1 = pt_x(deg, R_NEEDLE), y1 = pt_y(deg, R_NEEDLE);
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fill(x0, y0, 2, 2, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void GaugeView::draw_static() noexcept {
    fill(0, 0, kLcdW, kLcdH, C_BG);
    fill(0, 0, kLcdW, 12, C_TITLE);
    text(3, 3, "FLEETBUS  BODY NODE", 1, C_WHITE);
    for (int deg = 0; deg <= 180; deg += 3) {
        const int32_t mv = SCALE_LO_MV + static_cast<int32_t>(deg) * (SCALE_HI_MV - SCALE_LO_MV) / 180;
        const uint16_t c = mv < FAULT_MV ? C_RED : mv < WARN_MV ? C_AMBER : C_GREEN;
        fill(pt_x(deg, R_ZONE) - 2, pt_y(deg, R_ZONE) - 2, 4, 4, c);
    }
    text(TXT_X, 22, "TEMP", 1, C_LABEL);
    text(TXT_X, 50, "CURR", 1, C_LABEL);
    text(TXT_X, 78, "SOC", 1, C_LABEL);
}

void GaugeView::draw(const GaugeInputs& in) noexcept {
    if (!drawn_) { draw_static(); drawn_ = true; }

    const int deg = in.have ? volt_to_deg(in.pack_mv) : -1;
    if (deg != needle_deg_) {
        if (needle_deg_ >= 0) needle(needle_deg_, C_BG);              // erase the old needle only
        if (deg >= 0) needle(deg, C_WHITE);
        needle_deg_ = deg;
    }
    fill(CX - 3, CY - 3, 7, 7, C_WHITE);                              // hub (also covers the needles' shared start)

    Text v{}, t{}, a{}, s{};
    if (in.have) {
        fmt_fixed(v, in.pack_mv, 1000, 2, 'V');
        fmt_fixed(t, in.temp_dc, 10, 1, 'C');
        fmt_fixed(a, in.ma, 1000, 2, 'A');
        const uint8_t soc = in.soc_pct > 100 ? 100 : in.soc_pct;
        std::size_t n = 0;
        if (soc >= 100) s[n++] = '1';
        if (soc >= 10) s[n++] = static_cast<char>('0' + (soc / 10) % 10);
        s[n++] = static_cast<char>('0' + soc % 10);
        s[n++] = '%';
        s[n] = 0;
    } else {
        set_text(v, "-.--V"); set_text(t, "--.-C"); set_text(a, "-.--A"); set_text(s, "--%");
    }
    field(volt_, v, CX - 36, CY + 8, 72, 2);
    field(temp_, t, TXT_X, 33, kLcdW - TXT_X, 1);
    field(curr_, a, TXT_X, 61, kLcdW - TXT_X, 1);
    field(soc_, s, TXT_X, 89, kLcdW - TXT_X, 1);

    const Banner b = banner_for(in);
    if (b != banner_) {
        fill(0, BANNER_Y, kLcdW, kLcdH - BANNER_Y, BANNER_COL[b]);
        const char* bt = BANNER_TXT[b];
        text((kLcdW - text_width(bt, 1)) / 2, BANNER_Y + 6, bt, 1, b == B_NODATA ? C_WHITE : C_BG);
        banner_ = b;
    }
}

}  // namespace fleetbus
