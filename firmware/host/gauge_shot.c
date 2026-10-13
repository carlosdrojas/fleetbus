/* Host CLI: render the body-node instrument cluster (common/gauge_view.c, the code the board runs) to binary PPM
 * images, one per scenario, so docs can show the real output. tools/gauge_shots.py turns them into PNGs.
 *   gauge_shot <out_dir> */
#include <stdio.h>
#include <string.h>
#include "gauge_view.h"

static uint16_t fb[GV_H][GV_W];
static void fill_fb(void *ctx, int x, int y, int w, int h, uint16_t c) {
    (void)ctx;
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) fb[j][i] = c;
}

static int shot(const char *dir, const char *name, gv_inputs_t in) {
    gv_t g; gv_init(&g, (gv_ops_t){ fill_fb, 0 }); gv_draw(&g, &in);
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", GV_W, GV_H);
    for (int y = 0; y < GV_H; y++) for (int x = 0; x < GV_W; x++) {
        uint16_t c = fb[y][x];
        unsigned char rgb[3] = { (unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                 (unsigned char)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: gauge_shot <out_dir>\n"); return 2; }
    gv_inputs_t n = { .have = 1, .pack_mv = 3712, .ma = 842, .temp_dc = 251, .soc_pct = 59, .battery_alive = 1, .battery_state = FB_ST_NORMAL };
    gv_inputs_t w = n; w.pack_mv = 3180; w.soc_pct = 15; w.battery_state = FB_ST_WARNING;
    gv_inputs_t s = n; s.battery_alive = 0; s.limp_home = 1;
    gv_inputs_t z = { 0 };
    return shot(argv[1], "lcd_normal", n) | shot(argv[1], "lcd_warning", w) | shot(argv[1], "lcd_silent", s) | shot(argv[1], "lcd_nodata", z);
}
