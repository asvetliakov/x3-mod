#pragma once
#include <cstdint>
#include <limits>

namespace x3m::ownership {
// Fixed CPU metadata. Caller serializes all operations; no native access,
// payload, allocation, clock, or lock is hidden in this core.
struct BufferLockObservation {
    std::uint64_t allocation_id = 0, attempt_serial = 0, unlock_serial = 0;
    std::uint64_t readonly_attempts = 0, writable_attempts = 0, ordinary_attempts = 0;
    std::uint64_t discard_attempts = 0, nooverwrite_attempts = 0;
    std::uint64_t failed_locks = 0, failed_unlocks = 0;
    std::uint64_t revision = 0;
    std::uint32_t in_flight_locks = 0, in_flight_unlocks = 0, pending_locks = 0;
    std::uint32_t last_offset = 0, last_size = 0, last_flags = 0, last_thread = 0, last_unlock_thread = 0;
    bool saturated = false, ambiguous = false;
    template <class T> void increment(T& value) noexcept {
        if (value == std::numeric_limits<T>::max())
            saturated = true;
        else
            ++value;
    }
    void begin_lock(std::uint32_t offset, std::uint32_t size, std::uint32_t flags, std::uint32_t thread) noexcept {
        increment(attempt_serial);
        increment(in_flight_locks);
        // D3DLOCK flag values are documented constants; independent of SDK headers.
        increment((flags & 0x10u) ? readonly_attempts : writable_attempts);
        if (!(flags & (0x10u | 0x2000u | 0x1000u))) increment(ordinary_attempts);
        if (flags & 0x2000u) increment(discard_attempts);
        if (flags & 0x1000u) increment(nooverwrite_attempts);
        last_offset = offset;
        last_size = size;
        last_flags = flags;
        last_thread = thread;
    }
    void begin_unlock(std::uint32_t thread = 0) noexcept {
        increment(unlock_serial);
        increment(in_flight_unlocks);
        last_unlock_thread = thread;
    }
    void complete_lock(bool success) noexcept {
        if (!success) increment(failed_locks);
        if (in_flight_locks)
            --in_flight_locks;
        else
            ambiguous = true;
    }
    void complete_unlock(bool success) noexcept {
        if (!success) increment(failed_unlocks);
        if (in_flight_unlocks)
            --in_flight_unlocks;
        else
            ambiguous = true;
    }
    bool quiet() const noexcept {
        return allocation_id && !saturated && !ambiguous && !pending_locks && !in_flight_locks && !in_flight_unlocks;
    }
};
} // namespace x3m::ownership
