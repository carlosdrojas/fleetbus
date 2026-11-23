#pragma once
// Two real nodes on a perfect virtual bus, stepped in 10 ms ticks like the node task. Frames one node sends reach
// the other in the same tick. `battery_unplugged` cuts the battery off the bus (it keeps running, nobody hears it).
#include <cstdint>
#include <vector>
#include "node.hpp"

namespace fleetbus::test {

struct SimBus {
    BatteryNode battery{1};
    BodyNode body{1};
    uint32_t now = 1000;
    BatterySample sample{3800, 500, 250, true};
    bool battery_unplugged = false;
    std::vector<CanFrame> log;                  // every frame that reached the wire

    SimBus() { battery.start(now); body.start(now); }

    void run(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += kTickMs) {
            now += kTickMs;
            battery.tick(now, sample);
            body.tick(now);
            CanFrame f;
            while (battery.pop_tx(f)) if (!battery_unplugged) { log.push_back(f); body.on_can(now, f); }
            while (body.pop_tx(f)) { log.push_back(f); battery.on_can(now, f); }
        }
    }
    // Run until `cond` holds or `limit_ms` passes; returns the elapsed ms.
    template <typename Cond> uint32_t run_until(Cond cond, uint32_t limit_ms) {
        const uint32_t t0 = now;
        while (!cond() && now - t0 < limit_ms) run(kTickMs);
        return now - t0;
    }
};

}  // namespace fleetbus::test
