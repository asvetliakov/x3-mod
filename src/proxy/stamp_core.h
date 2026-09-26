#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

// Value-only pieces shared by the accumulate-only stamp groups
// (pass_phases_core.h, loop_phases_core.h): the owner gate and the
// nearest-rank percentile of a fixed window. No Win32, no allocation, no
// locking.
namespace x3m::stamp {
// Owner admission: the frame boundary (the frame-phase guard, Present thread)
// admits its thread once; a stamp from any other thread is `foreign`, a stamp
// before the first frame boundary is `early`; both are counted and ignored.
// Per stamp this is one relaxed load and a compare.
struct Gate {
    std::atomic<std::uint32_t> owner{0};
    std::atomic<std::uint32_t> early{0}, foreign{0};
    bool admit(std::uint32_t thread) noexcept {
        std::uint32_t expected = 0;
        owner.compare_exchange_strong(expected, thread, std::memory_order_acq_rel);
        return owner.load(std::memory_order_acquire) == thread;
    }
    bool owned(std::uint32_t thread) noexcept {
        const std::uint32_t current = owner.load(std::memory_order_relaxed);
        if (current == thread) return true;
        (current ? foreign : early).fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    void reset() noexcept {
        owner.store(0);
        early.store(0);
        foreign.store(0);
    }
};

// Nearest-rank percentile p of values[0, count) through a caller-owned scratch
// copy of at least `count` entries: one std::nth_element, no allocation.
inline std::uint64_t percentile(const std::uint64_t* values, unsigned count, unsigned p,
                                std::uint64_t* scratch) noexcept {
    if (!count) return 0;
    std::copy(values, values + count, scratch);
    std::size_t index = (static_cast<std::size_t>(count) * p) / 100;
    if (index >= count) index = count - 1;
    std::nth_element(scratch, scratch + index, scratch + count);
    return scratch[index];
}
}
