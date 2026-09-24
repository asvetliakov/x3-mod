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
// Camera-relative thin-region gate (docs/architecture/taa-lattice-crawl.md section 32.1): src/temporal/line_mask_camera_ps.hlsl,
// resolve_far_camera.hlsl and thin_box_ps.hlsl (manifests verification/results/temporal-{line-mask-camera,resolve-far-camera,thin-box}-program.json).
inline constexpr std::uint32_t temporal_line_mask_camera_words[] = {
#include "temporal_line_mask_camera_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_far_camera_words[] = {
#include "temporal_resolve_far_camera_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_words[] = {
#include "temporal_thin_box_program_inc.h"
};
// The mask chain's first draw with the current depth copy folded in (docs/architecture/taa-high-resolution.md S1):
// src/temporal/line_mask_depth_ps.hlsl and line_mask_camera_depth_ps.hlsl, COLOR1 = the current-depth texel
// (manifests verification/results/temporal-line-mask{,-camera}-depth-program.json).
inline constexpr std::uint32_t temporal_line_mask_depth_words[] = {
#include "temporal_line_mask_depth_program_inc.h"
};
inline constexpr std::uint32_t temporal_line_mask_camera_depth_words[] = {
#include "temporal_line_mask_camera_depth_program_inc.h"
};
// Sentinel stabiliser (docs/architecture/temporal-integration.md "Distant unrouted stations under a pan"): the separable box,
// src/temporal/thin_box_rows_ps.hlsl and thin_box_columns_ps.hlsl (manifests verification/results/temporal-thin-box-{rows,columns}-program.json).
inline constexpr std::uint32_t temporal_thin_box_rows_words[] = {
#include "temporal_thin_box_rows_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_columns_words[] = {
#include "temporal_thin_box_columns_program_inc.h"
};
// S3 (docs/architecture/taa-high-resolution.md): the five resolve programs above reconstruct the history with the 5-tap
// bilinear Catmull-Rom form; these keep the 16-tap point form of the earlier builds (X3M_HISTORY_TAPS16,
// src/temporal/resolve*_taps16.hlsl; manifests verification/results/temporal-resolve*-taps16-program.json) for
// --taa-history-taps 16 and for devices that cannot filter the FP16 / R32F histories.
inline constexpr std::uint32_t temporal_resolve_taps16_words[] = {
#include "temporal_resolve_taps16_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_thin_taps16_words[] = {
#include "temporal_resolve_thin_taps16_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_age_taps16_words[] = {
#include "temporal_resolve_age_taps16_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_far_taps16_words[] = {
#include "temporal_resolve_far_taps16_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_far_camera_taps16_words[] = {
#include "temporal_resolve_far_camera_taps16_program_inc.h"
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
inline constexpr const auto& temporal_resolve_thin_program() noexcept { return detail::temporal_resolve_thin_words; }
inline constexpr const auto& temporal_resolve_age_program() noexcept { return detail::temporal_resolve_age_words; }
// The mask and far-stabiliser programs TemporalPass::configure_far creates.
inline constexpr const auto& temporal_line_mask_program() noexcept { return detail::temporal_line_mask_words; }
inline constexpr const auto& temporal_resolve_far_program() noexcept { return detail::temporal_resolve_far_words; }
// The camera-gate programs TemporalPass::configure_far creates on top (optional: a refusal leaves the screen-speed gate).
inline constexpr const auto& temporal_line_mask_camera_program() noexcept { return detail::temporal_line_mask_camera_words; }
inline constexpr const auto& temporal_resolve_far_camera_program() noexcept { return detail::temporal_resolve_far_camera_words; }
inline constexpr const auto& temporal_thin_box_program() noexcept { return detail::temporal_thin_box_words; }
// The depth-folding mask programs configure_far creates on top (optional: a refusal keeps the copy draw).
inline constexpr const auto& temporal_line_mask_depth_program() noexcept { return detail::temporal_line_mask_depth_words; }
inline constexpr const auto& temporal_line_mask_camera_depth_program() noexcept { return detail::temporal_line_mask_camera_depth_words; }
inline constexpr const auto& temporal_thin_box_rows_program() noexcept { return detail::temporal_thin_box_rows_words; }
inline constexpr const auto& temporal_thin_box_columns_program() noexcept { return detail::temporal_thin_box_columns_words; }
// The 16-tap point twins TemporalPass creates beside the 5-tap programs (configure_history_taps).
inline constexpr const auto& temporal_resolve_taps16_program() noexcept { return detail::temporal_resolve_taps16_words; }
inline constexpr const auto& temporal_resolve_thin_taps16_program() noexcept { return detail::temporal_resolve_thin_taps16_words; }
inline constexpr const auto& temporal_resolve_age_taps16_program() noexcept { return detail::temporal_resolve_age_taps16_words; }
inline constexpr const auto& temporal_resolve_far_taps16_program() noexcept { return detail::temporal_resolve_far_taps16_words; }
inline constexpr const auto& temporal_resolve_far_camera_taps16_program() noexcept { return detail::temporal_resolve_far_camera_taps16_words; }
} // namespace x3m::renderer
