#pragma once
#include <cstdint>
#include "chase_camera_math.h"
// Main-gun predictive marker and optional native central instruments in a
// currently applied chase view. Requires
// chase_transition's actual cockpit lifetime and update hooks. No mode writes.
// FL edition (src/engine/fl/chase_lead_fl.cpp): the same API; its update binding is chase_transition's lifetime/update
// token as on AP when that module is installed (update=lifetime), else the chase camera's pose-seam visit
// (invalidate_pose, then camera_context), closed at the final-FOV seam (update=pose_seam); native_timing_invalidate is
// a no-op there.
namespace x3m::chase_lead {
bool initialize();
bool installed();
// Every pose-handler visit first invalidates its old witness, including errors
// and monitor visits. Only a successful camera write publishes a new witness.
void invalidate_pose();
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, bool applied,
                    const chase::Mat3* camera_basis = nullptr, const chase::Mat3* view_rel = nullptr);
// Called by the actual cockpit lifecycle/update boundaries; owns no engine data.
void native_timing_invalidate(std::uintptr_t cockpit, std::uint32_t thread) noexcept;
void report(std::uint64_t frame);
void shutdown(); // explicit quiescent fixture teardown only
#if defined(X3M_CHASE_LEAD_FL_FIXTURE) && X3M_EDITION_FL
// verification/probe/fl_chase_lead.cpp only (src/engine/fl/chase_lead_fl.cpp): the image base initialize() uses instead
// of fl::module_base(), a fresh initialize() (sites, pose, tickets and counters cleared; the arena kept), a one-shot
// foreign write into seam `site` (0 gate, 1 publish, 2 final FOV, 3 HUD gate) at `offset` and `second` (6 = none)
// between its claim and the read-back, and the reason counters (lead/HUD indices: ready, committed, finalized, no_pose, lifetime, read, scope,
// identity, projection, marker, hidden, unchanged).
struct FixtureCounts {
    std::uint64_t lead[12]{}, hud[12]{}, anchor_applied = 0, anchor_refused = 0;
    std::int32_t anchor_x = 0, anchor_y = 0;
    bool pose_open = false, lifetime = false; // lifetime: update=lifetime was chosen at initialize()
};
void fixture_base(std::uintptr_t base);
void fixture_reset();
void fixture_fault_readback(unsigned site, unsigned offset, unsigned second = 6);
FixtureCounts fixture_counts();
bool hud_installed();
void fixture_enable(bool lead, bool hud); // the flags the stubs test before their CPU envelope
#endif
} // namespace x3m::chase_lead
