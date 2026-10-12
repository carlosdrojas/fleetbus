/* MOCK(M1): layout matches the draft DBC but is not generated or tested against it. */
#include "can_msgs.h"

void fb_heartbeat_pack(const fb_heartbeat_t *m, uint8_t out[8]) {
    out[0] = m->node_id; out[1] = m->proto_ver;
    out[2] = m->fw_version & 0xFF; out[3] = m->fw_version >> 8;
    out[4] = m->uptime_s & 0xFF; out[5] = (m->uptime_s >> 8) & 0xFF; out[6] = (m->uptime_s >> 16) & 0xFF;
    out[7] = m->state;
}

void fb_heartbeat_unpack(const uint8_t in[8], fb_heartbeat_t *m) {
    m->node_id = in[0]; m->proto_ver = in[1];
    m->fw_version = (uint16_t)(in[2] | (in[3] << 8));
    m->uptime_s = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16);
    m->state = in[7];
}
