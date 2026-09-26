#pragma once
// One-cascade depth replay bookkeeping of the motion route
// (docs/architecture/shadow-replay-gates.md, "Implemented: cascade-0 depth
// replay fixture"; X3M_SHADOW_REPLAY_DEPTH=1). Per leased caster candidate the
// route retains what the replay re-issues: the application's own buffers,
// declaration and draw arguments (leased by native AddRef, never copied), the
// unjittered clip rows and the frame's sun constant. CPU storage only; the
// pass (src/renderer/shadow_replay_pass.h) performs the transaction.
#include <cstdint>
#include <d3d9.h>
#include "shadow_replay_candidates.h"
#include "shadow_replay_sun.h"

namespace x3m::shadow_replay {
constexpr unsigned depth_reason_count = 3;                       // lease, state, caps
constexpr unsigned depth_refusal_log_limit = 8;                  // refusal samples per reason per device
constexpr const char* depth_sun_constant_name = "LightDir_Dir0"; // world space, object->light; its register is per
                                                                 // program (shadow_replay_sun.h)
enum class DepthReason : unsigned { Lease = 0, State = 1, Caps = 2 };
constexpr const char* depth_reason_name(DepthReason r) noexcept {
    switch (r) {
    case DepthReason::Lease: return "lease";
    case DepthReason::State: return "state";
    default: return "caps";
    }
}
// Geometry lease of one candidate record (parallel to Frame::records).
struct DepthGeometry {
    IDirect3DVertexBuffer9* vertex_buffer = nullptr;    // AddRef'd at the draw, released after the scene end
    IDirect3DIndexBuffer9* index_buffer = nullptr;      // AddRef'd when indexed
    IDirect3DVertexDeclaration9* declaration = nullptr; // from GetVertexDeclaration (its own reference)
    UINT stream_offset = 0, stride = 0;
    D3DPRIMITIVETYPE topology = D3DPT_TRIANGLELIST;
    UINT primitives = 0, first = 0, min_vertex = 0, vertex_count = 0;
    INT base_vertex = 0;
    bool indexed = false, leased = false, sun_known = false; // sun_known: the program's own LightDir_Dir0 sample agreed
                                                             // with the frame's validated sun
    std::int8_t sun_register = -1; // the bound program's LightDir_Dir0 register (-1: none, or beyond the shadowed
                                   // range)
    bool multistream = false; // refused: the declaration references a stream other than 0, or stream 0 is instanced
    DWORD cull_mode = D3DCULL_NONE;
    float rows[16]{}; // the application's own clip rows (pre-jitter)
    float sun[4]{};   // that register's value at the draw (diagnostics; the replay uses the frame's one validated sun)
    // Alpha-tested caster (X3M_SHADOW_ALPHA_CASTERS): the draw's stage-0 texture, its own
    // GetTexture reference (the lease, released with the others), and the discard threshold.
    IDirect3DBaseTexture9* alpha_texture = nullptr;
    float alpha_threshold = 0.f;
};
struct DepthCounts {
    std::uint32_t draws = 0, replayed = 0, skipped_lease = 0, skipped_state = 0, skipped_caps = 0;
    double us = 0; // one QPC pair around the transaction (capture to restore)
};
} // namespace x3m::shadow_replay
