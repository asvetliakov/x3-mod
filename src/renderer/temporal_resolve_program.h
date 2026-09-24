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
// Camera-relative thin-region gate (docs/architecture/taa-lattice-crawl.md section 32.1): the tests draw of
// src/temporal/line_mask_camera_ps.hlsl (manifest verification/results/temporal-line-mask-camera-program.json); its resolve
// and box programs are the A' programs below.
inline constexpr std::uint32_t temporal_line_mask_camera_words[] = {
#include "temporal_line_mask_camera_program_inc.h"
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
// Thin-vote twins of the two (X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md section 3.2):
// src/temporal/line_mask_depth_thin_ps.hlsl and line_mask_camera_depth_thin_ps.hlsl, the same draws plus the vote from the lane's .a.
inline constexpr std::uint32_t temporal_line_mask_depth_thin_words[] = {
#include "temporal_line_mask_depth_thin_program_inc.h"
};
inline constexpr std::uint32_t temporal_line_mask_camera_depth_thin_words[] = {
#include "temporal_line_mask_camera_depth_thin_program_inc.h"
};
// S3 (docs/architecture/taa-high-resolution.md): the four resolve programs above reconstruct the history with the 5-tap
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
// A' (docs/architecture/taa-plan-lifted-slot-cap.md step 1, the only camera-gate path since Run 79 A): the camera-gate resolve
// with the region and closure holds, which composes the region from the mask's tests target itself
// (src/temporal/resolve_far_camera_hold.hlsl), the 49-tap box and the sentinel stabiliser's separable box, gated on that
// target (thin_box{,_rows,_columns}_hold_ps.hlsl; manifests
// verification/results/temporal-{resolve-far-camera,thin-box,thin-box-rows,thin-box-columns}-hold-program.json).
inline constexpr std::uint32_t temporal_resolve_far_camera_hold_words[] = {
#include "temporal_resolve_far_camera_hold_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_hold_words[] = {
#include "temporal_thin_box_hold_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_rows_hold_words[] = {
#include "temporal_thin_box_rows_hold_program_inc.h"
};
inline constexpr std::uint32_t temporal_thin_box_columns_hold_words[] = {
#include "temporal_thin_box_columns_hold_program_inc.h"
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
// The camera-gate programs TemporalPass::configure_far creates on top (optional: a refusal leaves no camera-gate path):
// the tests-draw mask here, the hold resolve and the box below.
inline constexpr const auto& temporal_line_mask_camera_program() noexcept { return detail::temporal_line_mask_camera_words; }
// The depth-folding mask programs configure_far creates on top (optional: a refusal keeps the copy draw).
inline constexpr const auto& temporal_line_mask_depth_program() noexcept { return detail::temporal_line_mask_depth_words; }
inline constexpr const auto& temporal_line_mask_camera_depth_program() noexcept { return detail::temporal_line_mask_camera_depth_words; }
inline constexpr const auto& temporal_line_mask_depth_thin_program() noexcept { return detail::temporal_line_mask_depth_thin_words; }
inline constexpr const auto& temporal_line_mask_camera_depth_thin_program() noexcept { return detail::temporal_line_mask_camera_depth_thin_words; }
// The 16-tap point twins TemporalPass creates beside the 5-tap programs (configure_history_taps).
inline constexpr const auto& temporal_resolve_taps16_program() noexcept { return detail::temporal_resolve_taps16_words; }
inline constexpr const auto& temporal_resolve_thin_taps16_program() noexcept { return detail::temporal_resolve_thin_taps16_words; }
inline constexpr const auto& temporal_resolve_age_taps16_program() noexcept { return detail::temporal_resolve_age_taps16_words; }
inline constexpr const auto& temporal_resolve_far_taps16_program() noexcept { return detail::temporal_resolve_far_taps16_words; }
// The camera-gate resolve and box configure_far creates with the camera mask (the camera gate has no 16-tap form), and the
// separable box's twins configure_sentinel creates.
inline constexpr const auto& temporal_resolve_far_camera_hold_program() noexcept { return detail::temporal_resolve_far_camera_hold_words; }
inline constexpr const auto& temporal_thin_box_hold_program() noexcept { return detail::temporal_thin_box_hold_words; }
inline constexpr const auto& temporal_thin_box_rows_hold_program() noexcept { return detail::temporal_thin_box_rows_hold_words; }
inline constexpr const auto& temporal_thin_box_columns_hold_program() noexcept { return detail::temporal_thin_box_columns_hold_words; }
} // namespace x3m::renderer
