// Stored-density fog: the cold-start hand-over report (docs/architecture/fog-handover.md,
// "Implementation"). Portable, no dependencies; shared by the cache and FogPass.
#pragma once
#include <cstdint>

namespace x3m {
namespace fog {

// One report per cold start (configure with a new identity, or invalidate), due in the frame the
// far readiness first reaches 1. Times are microseconds from the cold start; -1 is unmeasured.
struct HandoverReport {
    bool due = false;
    bool step = false, cold_fill = false; // the switches in force at the cold start
    bool whole_atlas = false;             // the far level went up in one whole-atlas latch
    std::uint64_t arm_frame = 0, drawable_frame = 0, ready_frame = 0;
    std::int64_t ready_us = 0;      // cold start -> far readiness 1 (the card hand-over)
    std::int64_t drawable_us = -1;  // cold start -> far need box resident on the GPU
    std::int64_t fill_us = -1;      // cold start -> the worker published the far need box
    std::int64_t fill_busy_us = -1; // worker generation wall time until then
    std::int64_t fill_cpu_us = -1;  // worker thread CPU time until then (-1 unmeasured)
    std::int64_t busy_us = 0;       // worker generation wall time, cold start -> drawable
    unsigned latches = 0;           // take_uploads calls that handed out rectangles, until drawable
    std::uint64_t upload_bytes = 0; // bytes those latches handed out
};

} // namespace fog
} // namespace x3m
