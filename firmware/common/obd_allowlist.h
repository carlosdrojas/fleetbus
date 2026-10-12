#pragma once
#include <stddef.h>
#include <stdint.h>
/* Never-write-to-the-car guard. The ONLY frames the telematics node may transmit on the car's bus are
 * OBD-II mode 01 (current data) and mode 09 (vehicle info) functional requests to 0x7DF for allowlisted PIDs.
 * Every transmit path in the car-facing code goes through fb_obd_build_request(); there is no raw send. */
#define FB_OBD_REQ_ID 0x7DFu
int fb_obd_pid_allowed(uint8_t mode, uint8_t pid);
/* Builds an 8-byte request frame. Returns 0 on success, -1 (and leaves out untouched) if not allowed. */
int fb_obd_build_request(uint8_t mode, uint8_t pid, uint8_t out[8]);
/* Returns 1 only for a frame this module could have built. Use as a TX filter. */
int fb_obd_frame_allowed(uint32_t id, const uint8_t *data, size_t len);
