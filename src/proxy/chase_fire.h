#pragma once
#include <cstdint>

// Consumer-local extension of cursor fire for the applied external chase view.
// It never changes global cursor/steering state or native gun/range/cone checks.
// Installation follows a successful camera hook, before the install window
// closes, and does not depend on telemetry. One exact JZ is relocated.
namespace x3m::chase_fire {
bool initialize();
bool installed();
// Active-player visits only; a refusal/internal/other view clears the witness.
// stamp is the successful camera update's QPC value (zero fails closed).
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, std::uint32_t mode,
                    std::uint32_t connect, std::uint32_t flags_1a0, bool applied, std::uint64_t stamp);
// Unknown/inactive visits invalidate only a previously accepted same cockpit.
void invalidate_camera(std::uintptr_t cockpit);
void report(std::uint64_t frame);
void shutdown();
#ifdef X3M_CHASE_FIRE_FL_FIXTURE
// The FL edition's fixture controls (src/engine/fl/chase_fire_fl.cpp; verification/probe/fl_chase_fire.cpp).
struct FixtureCounts {
    std::uint64_t result[6]{}, timed = 0, frequency = 0;
    std::uint32_t player = 0, early[5]{}, native_active = 0; // early: no witness, group, flags, other ship, read
};
void fixture_base(std::uintptr_t base);
void fixture_reset();
void fixture_fault_readback(unsigned offset, unsigned second = 6);
void fixture_age_witness(std::int64_t ticks);
void fixture_shift_thread();
void fixture_timing(bool on);
FixtureCounts fixture_counts();
#endif
}
