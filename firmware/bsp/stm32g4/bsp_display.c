/* No LCD is wired on the STM32 build (the Lite gauge cluster targets the MSPM0 body node). */
#include "bsp.h"
int bsp_display_init(void) { return -1; }
void bsp_display_fill(int x, int y, int w, int h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
