// Host CLI: render the body-node instrument cluster (common/gauge_view.cpp, the code the board runs) to binary PPM
// images, one per scenario, so docs can show the real output. tools/gauge_shots.py turns them into PNGs.
//   gauge_shot <out_dir>
#include <cstdio>
#include <string>
#include "gauge_view.hpp"

using namespace fleetbus;

namespace {
struct Framebuffer final : Canvas {
    uint16_t px[kLcdH][kLcdW]{};
    void fill(int x, int y, int w, int h, uint16_t c) override {
        for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px[j][i] = c;
    }
};

int shot(const std::string& dir, const char* name, const GaugeInputs& in) {
    static Framebuffer fb;
    GaugeView g{fb};
    g.draw(in);
    const std::string path = dir + "/" + name + ".ppm";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return 1;
    std::fprintf(f, "P6\n%d %d\n255\n", kLcdW, kLcdH);
    for (auto& row : fb.px) for (uint16_t c : row) {
        const unsigned char rgb[3] = { static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
                                       static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63),
                                       static_cast<unsigned char>((c & 31) * 255 / 31) };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("%s\n", path.c_str());
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: gauge_shot <out_dir>\n"); return 2; }
    GaugeInputs n;
    n.have = true; n.pack_mv = 3712; n.ma = 842; n.temp_dc = 251; n.soc_pct = 59; n.battery_alive = true; n.battery_state = FB_ST_NORMAL;
    GaugeInputs w = n; w.pack_mv = 3180; w.soc_pct = 15; w.battery_state = FB_ST_WARNING;
    GaugeInputs s = n; s.battery_alive = false; s.limp_home = true;
    const GaugeInputs z;
    return shot(argv[1], "lcd_normal", n) | shot(argv[1], "lcd_warning", w) | shot(argv[1], "lcd_silent", s) | shot(argv[1], "lcd_nodata", z);
}
