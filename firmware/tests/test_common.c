/* MOCK(M4): smoke tests only, not the real Unity suite. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "crc32.h"
#include "can_msgs.h"
#include "fault_fsm.h"
#include "hb_monitor.h"

int main(void) {
    assert(fb_crc32c((const uint8_t *)"123456789", 9) == 0xE3069283u); /* standard CRC-32C check value */
    fb_heartbeat_t a = {1, 1, 0x0102, 70000, 2}, b;
    uint8_t buf[8]; fb_heartbeat_pack(&a, buf); fb_heartbeat_unpack(buf, &b);
    assert(memcmp(&a, &b, sizeof a) == 0);
    fb_fault_fsm_t f; fb_fault_init(&f); fb_fault_step(&f, 3800, 250); assert(f.state == FB_NORMAL);
    fb_hb_monitor_t m; fb_hb_init(&m, 100); fb_hb_feed(&m, 10); assert(fb_hb_peer_alive(&m, 50)); assert(!fb_hb_peer_alive(&m, 500));
    puts("common smoke tests passed");
    return 0;
}
