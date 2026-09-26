#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/temporal-resolve-program.json.
inline constexpr std::uint32_t temporal_resolve_words[] = {
#include "temporal_resolve_program_inc.h"
};
// src/temporal/resolve_snapshot.hlsl: the reactive-mask snapshot modes (c7.z)
// (manifest verification/results/temporal-resolve-snapshot-program.json).
inline constexpr std::uint32_t temporal_resolve_snapshot_words[] = {
#include "temporal_resolve_snapshot_program_inc.h"
};
// Flicker-suppression variants (docs/architecture/taa-flicker-suppression.md;
// manifests verification/results/temporal-resolve-{thin,age}-program.json).
inline constexpr std::uint32_t temporal_resolve_thin_words[] = {
#include "temporal_resolve_thin_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_age_words[] = {
#include "temporal_resolve_age_program_inc.h"
};
// src/temporal/line_mask_ps.hlsl: the mask draws ahead of a far-stabiliser / thin-region resolve
// (manifest verification/results/temporal-line-mask-program.json).
inline constexpr std::uint32_t temporal_line_mask_words[] = {
#include "temporal_line_mask_program_inc.h"
};
// src/temporal/resolve_far.hlsl: the far-gated stabiliser (docs/architecture/taa-distant-line-fade.md section 9;
// manifest verification/results/temporal-resolve-far-program.json).
inline constexpr std::uint32_t temporal_resolve_far_words[] = {
#include "temporal_resolve_far_program_inc.h"
};
// The screen-gate chain's first draw with the current depth copy folded in (docs/architecture/taa-high-resolution.md
// S1): src/temporal/line_mask_depth_ps.hlsl, COLOR1 = the current-depth texel (manifest
// verification/results/temporal-line-mask-depth-program.json). The camera gate has no mask draw since the mask fold
// (docs/architecture/taa-mask-fold.md): its resolve computes the tests itself.
inline constexpr std::uint32_t temporal_line_mask_depth_words[] = {
#include "temporal_line_mask_depth_program_inc.h"
};
// Its thin-vote twin (X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md section 3.2):
// src/temporal/line_mask_depth_thin_ps.hlsl, the same draw plus the vote from the lane's .a.
inline constexpr std::uint32_t temporal_line_mask_depth_thin_words[] = {
#include "temporal_line_mask_depth_thin_program_inc.h"
};
// A' (docs/architecture/taa-plan-lifted-slot-cap.md step 1, the only camera-gate path since Run 79 A) with the mask
// fold (docs/architecture/taa-mask-fold.md): the camera-gate resolve with the region and closure holds, which computes
// the per-pixel tests and composes the region itself and writes the depth history as RT2
// (src/temporal/resolve_far_camera_hold.hlsl), and the 49-tap box gated on this frame's vote and last frame's region
// hold (thin_box_hold_ps.hlsl; manifests
// verification/results/temporal-{resolve-far-camera,thin-box}-hold-program.json).
inline constexpr std::uint32_t temporal_resolve_far_camera_hold_words[] = {
#include "temporal_resolve_far_camera_hold_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_hold_words[] = {
#include "temporal_thin_box_hold_program_inc.h"
};
// S4 (docs/architecture/taa-high-resolution.md S4, opt-in X3M_TAA_BOX_RESOLUTION=half): the same separable box at half
// resolution (thin_box_rows_half_ps.hlsl, thin_box_columns_half_ps.hlsl; manifests
// verification/results/temporal-thin-box-rows-half-program.json and temporal-thin-box-columns-half-program.json).
inline constexpr std::uint32_t temporal_thin_box_rows_half_words[] = {
#include "temporal_thin_box_rows_half_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_columns_half_words[] = {
#include "temporal_thin_box_columns_half_program_inc.h"
};
}
// Complete ps_3_0 program of src/temporal/resolve.hlsl (sampler and constant
// contract in src/temporal/README.md), the resolve the live route runs at the
// pre-bloom copy point (temporal step 3). The detached fixtures compile the
// HLSL themselves; the production DLL and the route fixture's reference pass
// use these words, so the two are bit-identical by construction. Storage is
// immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& temporal_resolve_program() noexcept {
    return detail::temporal_resolve_words;
}
// The mask-snapshot program TemporalPass creates itself (like the quad vertex
// program) and binds for the RequiredMask / Supplemental snapshot draws.
inline constexpr const auto& temporal_resolve_snapshot_program() noexcept {
    return detail::temporal_resolve_snapshot_words;
}
// The variants TemporalPass::configure_flicker creates: thin clip / alpha
// history (single target) and the same plus the age weight (COLOR1).
inline constexpr const auto& temporal_resolve_thin_program() noexcept {
    return detail::temporal_resolve_thin_words;
}
inline constexpr const auto& temporal_resolve_age_program() noexcept {
    return detail::temporal_resolve_age_words;
}
// The mask and far-stabiliser programs TemporalPass::configure_far creates.
inline constexpr const auto& temporal_line_mask_program() noexcept {
    return detail::temporal_line_mask_words;
}
inline constexpr const auto& temporal_resolve_far_program() noexcept {
    return detail::temporal_resolve_far_words;
}
// The depth-folding mask program configure_far creates on top (optional: a refusal keeps the copy draw) and its
// thin-vote twin (configure_thin_vote).
inline constexpr const auto& temporal_line_mask_depth_program() noexcept {
    return detail::temporal_line_mask_depth_words;
}
inline constexpr const auto& temporal_line_mask_depth_thin_program() noexcept {
    return detail::temporal_line_mask_depth_thin_words;
}
// The camera-gate resolve and box configure_far creates (optional: a refusal leaves no camera-gate path).
inline constexpr const auto& temporal_resolve_far_camera_hold_program() noexcept {
    return detail::temporal_resolve_far_camera_hold_words;
}
inline constexpr const auto& temporal_thin_box_hold_program() noexcept {
    return detail::temporal_thin_box_hold_words;
}
// The half-resolution pair TemporalPass::configure_box_resolution(2) creates (S4; none in a session that never asks).
inline constexpr const auto& temporal_thin_box_rows_half_program() noexcept {
    return detail::temporal_thin_box_rows_half_words;
}
inline constexpr const auto& temporal_thin_box_columns_half_program() noexcept {
    return detail::temporal_thin_box_columns_half_words;
}
} // namespace x3m::renderer
