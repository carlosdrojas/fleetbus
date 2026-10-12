#include "car_tx.h"
#include "bsp.h"
#include "obd_allowlist.h"

static volatile unsigned long blocked;

int car_tx(const fb_can_frame_t *f) {
    if (!fb_obd_frame_allowed(f->id, f->data, f->dlc)) { blocked++; return -1; }
    return bsp_can_send(f->id, f->data, f->dlc);
}
unsigned long car_tx_blocked(void) { return blocked; }
