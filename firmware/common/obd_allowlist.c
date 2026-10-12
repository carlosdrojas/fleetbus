#include "obd_allowlist.h"

/* Mode 01 PIDs: 0x04 load, 0x05 coolant, 0x0C rpm, 0x0D speed, 0x0F intake temp, 0x11 throttle, 0x42 control module voltage */
static const uint8_t mode01[] = {0x00, 0x04, 0x05, 0x0C, 0x0D, 0x0F, 0x11, 0x42};
/* Mode 09 PIDs: 0x00 supported, 0x02 VIN */
static const uint8_t mode09[] = {0x00, 0x02};

int fb_obd_pid_allowed(uint8_t mode, uint8_t pid) {
    const uint8_t *list; size_t n;
    if (mode == 0x01) { list = mode01; n = sizeof mode01; }
    else if (mode == 0x09) { list = mode09; n = sizeof mode09; }
    else return 0;
    for (size_t i = 0; i < n; i++) if (list[i] == pid) return 1;
    return 0;
}

int fb_obd_build_request(uint8_t mode, uint8_t pid, uint8_t out[8]) {
    if (!fb_obd_pid_allowed(mode, pid)) return -1;
    out[0] = 0x02; out[1] = mode; out[2] = pid;
    for (int i = 3; i < 8; i++) out[i] = 0x55;
    return 0;
}

int fb_obd_frame_allowed(uint32_t id, const uint8_t *data, size_t len) {
    if (id != FB_OBD_REQ_ID || len != 8 || data[0] != 0x02) return 0;
    for (int i = 3; i < 8; i++) if (data[i] != 0x55) return 0;
    return fb_obd_pid_allowed(data[1], data[2]);
}
