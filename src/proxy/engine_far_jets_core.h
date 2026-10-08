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

// One far jet as the pass left it: 72 bytes, raw integers. No scene-phase tag: the main view's cull pass may run before
// the scene-boundary selector enters its scene phase, so the plume stage decides by the view handle alone
// (motion_output_engine_plumes_inc.h engine_far_append and the view rule there).
struct Raw {
    std::uint32_t node, view_handle, context, parent;
    std::uint32_t handle, model;
    std::uint32_t scale70, scale80, scale88;
    std::int32_t position[3];
    std::int32_t basis_x[3], basis_z[3]; // 16.16 rows 0 and 2 of the node basis: model x and model z (c4-6 order a)
};
static_assert(sizeof(Raw) == 72, "the raw entry");

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
    std::uint32_t culled_overflow = 0; // engine-culled pairs beyond the culled list's capacity
};
// A JET node the engine culled by size at the site (Stats::engine): its (node handle, view handle) pair, kept so the
// node-sourced walk (engine_nozzle_walk_core.h) does not resurrect a jet the game itself refused. Integer only, the
// handler's second per-frame list (docs/architecture/engine-nozzle-source.md section 2).
struct CulledPair {
    std::uint32_t handle, view_handle;
};

// The far block's arming across devices: each device that requests the plume stage counts once (its own `counted`
// flag), and the handler copies while at least one does, so an old device's teardown after a new device configured
// itself lowers the count instead of disarming the session. The device that owns the resolve (the requesting device
// whose resolve last took the stage's arming decision: claim) empties the buffer at its frame begin; with no owner
// (none resolved yet, or the owner withdrew) every requesting device does. Device identities are opaque, non-zero.
struct Requests {
    unsigned devices = 0;
    std::uintptr_t owner = 0;
    void request(bool* counted, bool requested, std::uintptr_t device) noexcept {
        if (requested == *counted) return;
        *counted = requested;
        if (requested) {
            ++devices;
            return;
        }
        if (devices) --devices;
        if (owner == device) owner = 0;
    }
    bool armed() const noexcept { return devices != 0; }
    void claim(std::uintptr_t device) noexcept { owner = device; }
    bool clears(std::uintptr_t device) const noexcept { return !owner || owner == device; }
};

// The frame's (node handle, view handle) pairs at the plume stage's append: the cull pass may run more than once per
// view, so one jet can be copied twice under the same view. A generation-stamped open-addressing set of 2,048 slots,
// twice the buffer's capacity, so a probe always meets a free slot (a frame inserts at most `capacity` keys). begin()
// opens a frame without clearing (the stamps are cleared once when the generation wraps). insert() is true for a new
// pair and false for a duplicate; a zero node handle has no identity and is never a duplicate. 24 KB, integer only.
struct Seen {
    static constexpr unsigned slots = 2 * capacity;
    static_assert((slots & (slots - 1)) == 0, "a power of two");
    std::uint64_t key[slots];
    std::uint32_t stamp[slots];
    std::uint32_t generation = 1;
    Seen() noexcept : key{}, stamp{} {}
    void begin() noexcept {
        if (++generation) return;
        for (unsigned i = 0; i < slots; ++i) stamp[i] = 0;
        generation = 1;
    }
    bool insert(std::uint32_t node_handle, std::uint32_t view_handle) noexcept {
        if (!node_handle) return true;
        const std::uint64_t k = (std::uint64_t(node_handle) << 32) | view_handle;
        unsigned i = unsigned((k * 0x9e3779b97f4a7c15ull) >> 53); // the top 11 bits: 0 .. slots - 1
        for (unsigned n = 0; n < slots; ++n, i = (i + 1) & (slots - 1)) {
            if (stamp[i] != generation) {
                stamp[i] = generation;
                key[i] = k;
                return true;
            }
            if (key[i] == k) return false;
        }
        return true; // unreachable at <= capacity inserts per generation: kept rather than dropped
    }
};
} // namespace x3m::engine_far_jets::core
