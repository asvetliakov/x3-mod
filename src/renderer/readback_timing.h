#pragma once
#include <cstdint>

namespace x3m::renderer {
// Exhaustive CPU envelope of the double-buffered meter readback: LockRect of
// the surface the previous latch filled (including validation; Lock, logged as
// readback_lock_us, also includes the lock gate's GetData poll, alone on a
// skipped latch), tile extraction + UnlockRect, the GetRenderTargetData that
// fills the other surface for the next latch with its query's Issue
// (pending-slot retirement included), statistics +
// adaptation. Phases may close in any order; adjacent boundaries share a
// clock; any failed/backward clock invalidates all buckets, so a partial
// measurement cannot masquerade as the readback total.
struct ReadbackTiming {
    enum Phase : unsigned { Copy, Lock, ExtractUnlock, StatisticsAdapt, Count };
    std::uint64_t ticks[Count]{};
    std::uint64_t last = 0;
    unsigned clock_errors = 0;
    bool enabled = false;
    void begin(bool timing, std::uint64_t now) noexcept {
        enabled = timing;
        last = now;
        if (enabled && !now) ++clock_errors;
    }
    void end(Phase phase, std::uint64_t now) noexcept {
        if (!enabled) return;
        if (!now || !last || now < last) ++clock_errors;
        if (!clock_errors)
            ticks[phase] = now - last;
        else
            for (auto& t : ticks) t = 0;
        last = now;
    }
    std::uint64_t total() const noexcept {
        std::uint64_t sum = 0;
        for (const auto t : ticks) sum += t;
        return sum;
    }
};
}
