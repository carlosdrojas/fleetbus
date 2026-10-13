/* Tiny polled logger shared by every board: avoids newlib printf (flash) and runs before the RTOS starts.
 * Each board provides bsp_putc() on its log UART. */
#include <stdarg.h>
#include "bsp.h"

static void put_num(unsigned long v, unsigned base, int neg) {
    char b[12]; int n = 0;
    do { b[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v && n < 11);
    if (neg) bsp_putc('-');
    while (n) bsp_putc(b[--n]);
}
/* Supports %d %u %x %c %s %% with optional 'l'. Enough for status lines; not a general printf. */
void bsp_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { bsp_putc(*fmt); continue; }
        fmt++; if (*fmt == 'l') fmt++;
        switch (*fmt) {
        case 'd': { long v = va_arg(ap, long); put_num(v < 0 ? (unsigned long)-v : (unsigned long)v, 10, v < 0); break; }
        case 'u': put_num(va_arg(ap, unsigned long), 10, 0); break;
        case 'x': put_num(va_arg(ap, unsigned long), 16, 0); break;
        case 'c': bsp_putc((char)va_arg(ap, int)); break;
        case 's': { const char *s = va_arg(ap, const char *); while (s && *s) bsp_putc(*s++); break; }
        case '%': bsp_putc('%'); break;
        default: bsp_putc('?'); break;
        }
    }
    va_end(ap);
}

