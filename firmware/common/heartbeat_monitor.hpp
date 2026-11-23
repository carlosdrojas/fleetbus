#pragma once
// Peer liveness from heartbeats. A peer never heard from counts as dead (fail-safe at boot).
// Wrap-safe: unsigned subtraction gives the right age across the 32-bit millisecond wrap (~49.7 days).
#include <cstdint>

namespace fleetbus {

class HeartbeatMonitor {
public:
    explicit constexpr HeartbeatMonitor(uint32_t timeout_ms) noexcept : timeout_ms_(timeout_ms) {}

    void feed(uint32_t now_ms) noexcept { last_ms_ = now_ms; seen_ = true; }
    void forget() noexcept { seen_ = false; }

    [[nodiscard]] bool alive(uint32_t now_ms) const noexcept {
        return seen_ && static_cast<uint32_t>(now_ms - last_ms_) <= timeout_ms_;
    }
    [[nodiscard]] bool ever_seen() const noexcept { return seen_; }
    [[nodiscard]] uint32_t age_ms(uint32_t now_ms) const noexcept { return now_ms - last_ms_; }

private:
    uint32_t timeout_ms_;
    uint32_t last_ms_ = 0;
    bool seen_ = false;
};

}  // namespace fleetbus
