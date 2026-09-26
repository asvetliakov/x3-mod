#pragma once
#include <cstdint>
namespace x3m {
// No device calls or floating point. The first successful stacked frame arms
// the next frame. Any lost replacement after a suppression is terminal until
// Reset; toggling cannot erase that evidence.
struct FogCardPolicy {
    bool armed = false, fault = false, active = false, warmup = false;
    bool refused = false, finished = true;
    unsigned observed = 0, suppressed = 0;
    void begin(bool enabled) noexcept {
        if (suppressed && !finished) fault = true;
        active = enabled && !fault;
        if (!enabled) armed = false;
        warmup = active && !armed;
        refused = false;
        finished = false;
        observed = suppressed = 0;
    }
    bool may_replace() const noexcept { return active && !warmup && !refused && !fault; }
    // Stored range, cold-start step (fog-handover.md, "Implementation"): the medium reaches full density in this
    // frame with no ramp frame before it to warm up on, so the frame's own transaction is the proof: armed now,
    // before any card of the frame; finish() faults the replacement if this frame's pass then fails.
    void arm_on_cold_step() noexcept {
        if (active && warmup && !refused && !fault) {
            warmup = false;
            armed = true;
        }
    }
    bool medium_allowed() const noexcept { return active && (warmup || !refused); }
    void reject() noexcept { refused = true; }
    void fail() noexcept {
        fault = true;
        active = false;
        armed = false;
    }
    void finish(bool success) noexcept {
        finished = true;
        if (suppressed && !success) fail();
        if (active && warmup && success) armed = true;
    }
};
} // namespace x3m
