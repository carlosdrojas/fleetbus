#pragma once
// Fixed-capacity FIFO with no heap. One slot stays empty to tell full from empty, so it holds N - 1 items.
// Single context only: the node task both pushes and pops. Not an ISR-safe queue (FreeRTOS queues do that job).
#include <array>
#include <cstddef>
#include <cstdint>

namespace fleetbus {

template <typename T, std::size_t N>
class StaticQueue {
    static_assert(N >= 2 && N <= 256, "index type is uint8_t");

public:
    [[nodiscard]] bool push(const T& item) {
        const auto next = advance(head_);
        if (next == tail_) { ++dropped_; return false; }
        buf_[head_] = item;
        head_ = next;
        return true;
    }
    [[nodiscard]] bool pop(T& out) {
        if (empty()) return false;
        out = buf_[tail_];
        tail_ = advance(tail_);
        return true;
    }
    bool empty() const { return head_ == tail_; }
    std::size_t size() const { return (head_ + N - tail_) % N; }
    static constexpr std::size_t capacity() { return N - 1; }
    uint32_t dropped() const { return dropped_; }

private:
    static constexpr uint8_t advance(uint8_t i) { return static_cast<uint8_t>((i + 1u) % N); }
    std::array<T, N> buf_{};
    uint8_t head_ = 0, tail_ = 0;
    uint32_t dropped_ = 0;
};

}  // namespace fleetbus
