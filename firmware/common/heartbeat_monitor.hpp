#pragma once
// Peer liveness from heartbeats. A peer never heard from counts as dead (fail-safe at boot).
// Wrap-safe: unsigned subtraction gives the right age across the 32-bit millisecond wrap (~49.7 days).
#include <cstdint>

namespace fleetbus {

class HeartbeatMonitor {
public:
    explicit constexpr HeartbeatMonitor(uint32_t timeout_ms) : timeout_ms_(timeout_ms) {}

    void feed(uint32_t now_ms) { last_ms_ = now_ms; seen_ = true; }
    void forget() { seen_ = false; }

    bool alive(uint32_t now_ms) const {
        return seen_ && static_cast<uint32_t>(now_ms - last_ms_) <= timeout_ms_;
    }
    bool ever_seen() const { return seen_; }
    uint32_t age_ms(uint32_t now_ms) const { return now_ms - last_ms_; }

private:
    uint32_t timeout_ms_;
    uint32_t last_ms_ = 0;
    bool seen_ = false;
};

}  // namespace fleetbus
