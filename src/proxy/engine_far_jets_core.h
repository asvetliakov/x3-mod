#pragma once
#include <cstdint>

// Portable core of the far engine jets (X3M_ENGINE_EFFECTS=plumes with X3M_CULL_SMALL_PARTS_PX;
// docs/architecture/engine-effects-modern.md "After flight E", docs/verification/cull-small-parts.md "Far engine
// jets"): the small-parts cull stub keeps culling a far JET node (the engine's submission work per jet is what the cull
// saves) and calls x3m_engine_far_jet (engine_far_jets.cpp) on its cull path, which copies the node's raw fields into a
// per-frame buffer; the motion route turns them into plume records (engine_effects_core.h far_record) outside the pass.
// Integer only: the handler runs inside the engine's cull/LOD pass with no CPU-state boundary (no SSE, no x87, no
// Win32), so this header carries no floating point. No Windows dependency: the host tests compile it.
namespace x3m::engine_far_jets::core {
// Render-node fields the handler reads, every one inside the node block the pass itself dereferences (+0x18, +0xa0,
// +0x12c, +0x130, +0x140, +0x1d8, +0x1dc on the same node; the world fields are written by the node traversal
// 0x0047bc20 the frame routine calls at 0x00472256 before the view activation that precedes the pass;
// docs/reverse-engineering/engine-effects.md sections 4 and 6, chase-camera-first-flight.md).
constexpr unsigned parent_offset = 0x18, handle_offset = 0x28, scale70_offset = 0x70, scale80_offset = 0x80,
                   scale88_offset = 0x88, position_offset = 0xb0, basis_x_offset = 0xc0, basis_z_offset = 0xe0,
                   flags12c_offset = 0x12c, model_offset = 0x140, threshold_1d8_offset = 0x1d8;
// The pass's view (site [ESP+0x28], the 0x790-byte camera node whose +0x270 and +0x298 the pass reads): its handle (the
// object scope's camera handle the draw path tags records with, object_trace camera+0x28) and its context pointer
// (+0x1c; the context's float at +0x2c is the view's context scale, read outside the pass).
constexpr unsigned view_handle_offset = 0x28, view_context_offset = 0x1c, context_scale_offset = 0x2c;
constexpr std::uint32_t steering_model = 566; // v/00566: JET + SMALLJET, the RCS nozzle (engine_effects_core.h)
constexpr unsigned capacity = 1024;           // entries per frame (the record ring's capacity)

// One far jet as the pass left it: 76 bytes, raw integers.
struct Raw {
    std::uint32_t node, view_handle, context, parent;
    std::uint32_t handle, model;
    std::uint32_t scale70, scale80, scale88;
    std::int32_t position[3];
    std::int32_t basis_x[3], basis_z[3]; // 16.16 rows 0 and 2 of the node basis: model x and model z (c4-6 order a)
    std::uint32_t scene;                 // the scene-boundary selector was in its scene phase (the draw path's tag)
};
static_assert(sizeof(Raw) == 76, "the raw entry");

// The engine's own verdict at the site, mirroring 0x0047d2a2..0x0047d2c3 and the degenerate test after it (the census's
// culled_size and culled_min, cull_census_core.h classify): limit = max(node +0x1d8, parent +0x1d8 when a parent),
// culled when limit > 0 and the measure (ESI, zeroed in the env-map view) is below it, or when the measure is below 1
// and +0x12c lacks 0x4000000. Such a jet is one the game would not draw either: no record.
inline bool engine_culls(std::int32_t measure, std::int32_t own_1d8, std::uint32_t parent, std::int32_t parent_1d8,
                         std::uint32_t flags12c) noexcept {
    std::int32_t limit = own_1d8;
    if (parent && parent_1d8 > limit) limit = parent_1d8;
    if (limit > 0 && measure < limit) return true;
    return measure < 1 && !(flags12c & 0x4000000u);
}

// The per-frame counts (the engine_stage row's far_* fields).
struct Stats {
    std::uint32_t written = 0;  // entries copied this frame
    std::uint32_t engine = 0;   // JET nodes the engine would cull itself (engine_culls): no entry
    std::uint32_t steering = 0; // v/00566 nodes: no entry (the recogniser's RCS rule; SMALLJET table entries at conversion)
    std::uint32_t overflow = 0; // the buffer was full
    std::uint32_t disarmed = 0; // calls while no device requested the plume stage
};
} // namespace x3m::engine_far_jets::core
