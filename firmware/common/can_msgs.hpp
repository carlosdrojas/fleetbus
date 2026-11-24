#pragma once
// The CAN protocol: 4 messages, classic CAN 2.0A (11-bit ids), 500 kbit/s. Hand-written, little-endian.
// Both boards compile this one file, so they can't disagree about a layout.
//
//   id             message        sender   rate        bytes
//   0x101 / 0x102  Heartbeat      each     10 Hz       0 node id | 1-2 fw version | 3-5 uptime s | 6 state | 7 proto
//   0x200          BatteryStatus  battery  10 Hz       0-1 pack mV | 2-3 mA (signed) | 4-5 0.1 C (signed) | 6 SoC % | 7 proto
//   0x201          BodyStatus     body     10 Hz       0 lights | 1 servo deg | 2 limp-home | 3 proto          (dlc 4)
//   0x300          FaultEvent     battery  on fault,   0-1 fault code | 2 severity | 3 node id | 7 proto
//                                          then 1 Hz
// Lower id wins arbitration, so heartbeats outrank everything. The heartbeat id is 0x100 + node id: with one shared
// id, two nodes starting a heartbeat in the same bit time both win arbitration, then collide on the node-id byte.
// Every message ends with the protocol version; a receiver drops versions it doesn't know.
#include <cstdint>

namespace fleetbus::msg {

constexpr uint32_t kHeartbeatBase = 0x100;
constexpr uint32_t kBatteryStatusId = 0x200;
constexpr uint32_t kBodyStatusId = 0x201;
constexpr uint32_t kFaultEventId = 0x300;

constexpr uint8_t kHeartbeatDlc = 8, kBatteryStatusDlc = 8, kBodyStatusDlc = 4, kFaultEventDlc = 8;

// Little-endian field helpers. CAN doesn't define a byte order; this protocol picks little-endian, like most DBCs.
inline void put16(uint8_t* b, uint16_t v) noexcept { b[0] = static_cast<uint8_t>(v); b[1] = static_cast<uint8_t>(v >> 8); }
inline uint16_t get16(const uint8_t* b) noexcept { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }

struct Heartbeat {
    uint8_t node_id = 0;
    uint16_t fw_version = 0;
    uint32_t uptime_s = 0;        // 24 bits on the wire: wraps after 194 days
    uint8_t state = 0;            // fb_state_t
    uint8_t proto_ver = 0;

    void pack(uint8_t* b) const noexcept {
        b[0] = node_id;
        put16(b + 1, fw_version);
        b[3] = static_cast<uint8_t>(uptime_s); b[4] = static_cast<uint8_t>(uptime_s >> 8);
        b[5] = static_cast<uint8_t>(uptime_s >> 16);
        b[6] = state;
        b[7] = proto_ver;
    }
    [[nodiscard]] bool unpack(const uint8_t* b, uint8_t dlc) noexcept {
        if (dlc < kHeartbeatDlc) return false;
        node_id = b[0];
        fw_version = get16(b + 1);
        uptime_s = b[3] | (uint32_t{b[4]} << 8) | (uint32_t{b[5]} << 16);
        state = b[6];
        proto_ver = b[7];
        return true;
    }
};

struct BatteryStatus {
    uint16_t pack_mv = 0;
    int16_t ma = 0;               // signed: negative = charging
    int16_t temp_dc = 0;          // 0.1 C, signed
    uint8_t soc_pct = 0;
    uint8_t proto_ver = 0;

    void pack(uint8_t* b) const noexcept {
        put16(b, pack_mv);
        put16(b + 2, static_cast<uint16_t>(ma));   // two's complement bits, sent as-is
        put16(b + 4, static_cast<uint16_t>(temp_dc));
        b[6] = soc_pct;
        b[7] = proto_ver;
    }
    [[nodiscard]] bool unpack(const uint8_t* b, uint8_t dlc) noexcept {
        if (dlc < kBatteryStatusDlc) return false;
        pack_mv = get16(b);
        ma = static_cast<int16_t>(get16(b + 2));
        temp_dc = static_cast<int16_t>(get16(b + 4));
        soc_pct = b[6];
        proto_ver = b[7];
        return true;
    }
};

struct BodyStatus {
    uint8_t lights = 0;           // FB_LIGHT_* bitmask
    uint8_t servo_deg = 0;
    uint8_t limp_home = 0;
    uint8_t proto_ver = 0;

    void pack(uint8_t* b) const noexcept { b[0] = lights; b[1] = servo_deg; b[2] = limp_home; b[3] = proto_ver; }
    [[nodiscard]] bool unpack(const uint8_t* b, uint8_t dlc) noexcept {
        if (dlc < kBodyStatusDlc) return false;
        lights = b[0]; servo_deg = b[1]; limp_home = b[2]; proto_ver = b[3];
        return true;
    }
};

struct FaultEvent {
    uint16_t code = 0;            // fb_fault_code_t
    uint8_t severity = 0;         // 2 = FAULT, 3 = SAFE
    uint8_t node_id = 0;
    uint8_t proto_ver = 0;

    void pack(uint8_t* b) const noexcept {
        put16(b, code);
        b[2] = severity; b[3] = node_id;
        b[4] = b[5] = b[6] = 0;
        b[7] = proto_ver;
    }
    [[nodiscard]] bool unpack(const uint8_t* b, uint8_t dlc) noexcept {
        if (dlc < kFaultEventDlc) return false;
        code = get16(b); severity = b[2]; node_id = b[3]; proto_ver = b[7];
        return true;
    }
};

}  // namespace fleetbus::msg
