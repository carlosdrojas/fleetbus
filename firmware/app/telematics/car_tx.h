#pragma once
#include "node_core.h"
/* The ONLY function in the telematics firmware that may put a frame on the car's CAN bus. It refuses anything that
 * obd_allowlist.c would not itself have built (OBD-II mode 01/09 functional requests, allowlisted PIDs). tools/check_car_tx.sh
 * fails the build if any other source calls the raw CAN driver send function. */
int car_tx(const fb_can_frame_t *f);
unsigned long car_tx_blocked(void);
