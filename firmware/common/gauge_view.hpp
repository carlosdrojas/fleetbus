#pragma once
// Body-node instrument cluster for a 160x128 RGB565 display (Adafruit 1.8" ST7735R on the board).
// Hardware-free: it draws only through Canvas::fill, so the same code drives the panel over SPI (app_main.cpp) and
// a framebuffer in the host tests (tests/test_gauge.cpp).
//
// Layout: title bar; a 180-degree pack-voltage gauge (2.8-4.2 V, red/amber/green zones at the fault and warning
// thresholds) with a needle and the value; temperature, current and state of charge on the right; a state banner.
// The first draw() paints everything; later calls repaint only what changed, which keeps SPI traffic small.
#include <array>
#include <cstdint>
#include "fb_types.h"

namespace fleetbus {

constexpr int kLcdW = 160;
constexpr int kLcdH = 128;

constexpr uint16_t rgb565(unsigned r, unsigned g, unsigned b) noexcept {
    return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// Anything that can fill a rectangle. One virtual call per rectangle: negligible next to the SPI transfer it starts.
// Protected non-virtual destructor: never deleted through this interface, so no deleting destructor or heap needed.
class Canvas {
public:
    virtual void fill(int x, int y, int w, int h, uint16_t rgb565) = 0;
protected:
    ~Canvas() = default;
};

struct GaugeInputs {
    bool have = false;                  // a BatteryStatus has been received at all
    int32_t pack_mv = 0, ma = 0, temp_dc = 0;
    uint8_t soc_pct = 0;
    bool battery_alive = false;         // battery heartbeat seen within the peer timeout
    fb_state_t battery_state = FB_ST_BOOT;
    bool limp_home = false;             // the body node's own limp-home output
};

class GaugeView {
public:
    explicit GaugeView(Canvas& canvas) noexcept : canvas_(canvas) {}
    void draw(const GaugeInputs& in) noexcept;
    void invalidate() noexcept { drawn_ = false; needle_deg_ = -1; banner_ = -1; volt_[0] = temp_[0] = curr_[0] = soc_[0] = 0; }

    // Needle angle for a pack voltage: 0 deg at 2.8 V (left), 180 deg at 4.2 V (right), clamped.
    [[nodiscard]] static int volt_to_deg(int32_t pack_mv) noexcept;

private:
    using Text = std::array<char, 8>;
    void fill(int x, int y, int w, int h, uint16_t c) noexcept;
    void text(int x, int y, const char* s, int scale, uint16_t fg) noexcept;
    void field(Text& last, const Text& now, int x, int y, int box_w, int scale) noexcept;
    void needle(int deg, uint16_t c) noexcept;
    void draw_static() noexcept;

    Canvas& canvas_;
    bool drawn_ = false;
    int needle_deg_ = -1;               // last needle angle drawn, -1 = none
    int banner_ = -1;                   // last banner drawn, -1 = none
    Text volt_{}, temp_{}, curr_{}, soc_{};
};

}  // namespace fleetbus
