#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/temporal-resolve-program.json.
inline constexpr std::uint32_t temporal_resolve_words[] = {
#include "temporal_resolve_program_inc.h"
};
// src/temporal/resolve_filter.hlsl: the same source with X3M_CURRENT_FILTER
// (manifest verification/results/temporal-resolve-filter-program.json).
inline constexpr std::uint32_t temporal_resolve_filter_words[] = {
#include "temporal_resolve_filter_program_inc.h"
};
// src/temporal/resolve_snapshot.hlsl: the reactive-mask snapshot modes (c7.z)
// (manifest verification/results/temporal-resolve-snapshot-program.json).
inline constexpr std::uint32_t temporal_resolve_snapshot_words[] = {
#include "temporal_resolve_snapshot_program_inc.h"
};
// Flicker-suppression variants (docs/architecture/taa-flicker-suppression.md;
// manifests verification/results/temporal-resolve-{thin,thin-filter,age,age-filter}-program.json).
inline constexpr std::uint32_t temporal_resolve_thin_words[] = {
#include "temporal_resolve_thin_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_thin_filter_words[] = {
#include "temporal_resolve_thin_filter_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_age_words[] = {
#include "temporal_resolve_age_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_age_filter_words[] = {
#include "temporal_resolve_age_filter_program_inc.h"
};
// Line-masked filtered current sample (docs/architecture/taa-lattice-crawl.md section 9;
// manifests verification/results/temporal-resolve-{line,thin-line,age-line}-program.json).
// src/temporal/line_mask_ps.hlsl: the two mask draws ahead of a line-filtered resolve
// (manifest verification/results/temporal-line-mask-program.json).
inline constexpr std::uint32_t temporal_line_mask_words[] = {
#include "temporal_line_mask_program_inc.h"
};
// src/temporal/resolve_far.hlsl: the far-gated stabiliser (docs/architecture/taa-distant-line-fade.md section 9;
// manifest verification/results/temporal-resolve-far-program.json).
inline constexpr std::uint32_t temporal_resolve_far_words[] = {
#include "temporal_resolve_far_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_line_words[] = {
#include "temporal_resolve_line_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_thin_line_words[] = {
#include "temporal_resolve_thin_line_program_inc.h"
};
inline constexpr std::uint32_t temporal_resolve_age_line_words[] = {
#include "temporal_resolve_age_line_program_inc.h"
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
// The variant TemporalPass binds when FrameInputs::current_filter > 0.
inline constexpr const auto& temporal_resolve_filter_program() noexcept {
    return detail::temporal_resolve_filter_words;
}
// The mask-snapshot program TemporalPass creates itself (like the quad vertex
// program) and binds for the RequiredMask / Supplemental snapshot draws.
inline constexpr const auto& temporal_resolve_snapshot_program() noexcept {
    return detail::temporal_resolve_snapshot_words;
}
// The variants TemporalPass::configure_flicker creates: thin clip / alpha
// history (single target) and the same plus the age weight (COLOR1), each with
// and without the filtered current sample.
inline constexpr const auto& temporal_resolve_thin_program() noexcept { return detail::temporal_resolve_thin_words; }
inline constexpr const auto& temporal_resolve_thin_filter_program() noexcept { return detail::temporal_resolve_thin_filter_words; }
inline constexpr const auto& temporal_resolve_age_program() noexcept { return detail::temporal_resolve_age_words; }
inline constexpr const auto& temporal_resolve_age_filter_program() noexcept { return detail::temporal_resolve_age_filter_words; }
// The variants TemporalPass::configure_line_filter creates.
inline constexpr const auto& temporal_line_mask_program() noexcept { return detail::temporal_line_mask_words; }
inline constexpr const auto& temporal_resolve_far_program() noexcept { return detail::temporal_resolve_far_words; }
inline constexpr const auto& temporal_resolve_line_program() noexcept { return detail::temporal_resolve_line_words; }
inline constexpr const auto& temporal_resolve_thin_line_program() noexcept { return detail::temporal_resolve_thin_line_words; }
inline constexpr const auto& temporal_resolve_age_line_program() noexcept { return detail::temporal_resolve_age_line_words; }
// The camera-gate programs TemporalPass::configure_far creates on top (optional: a refusal leaves the screen-speed gate).
inline constexpr const auto& temporal_line_mask_camera_program() noexcept { return detail::temporal_line_mask_camera_words; }
inline constexpr const auto& temporal_resolve_far_camera_program() noexcept { return detail::temporal_resolve_far_camera_words; }
inline constexpr const auto& temporal_thin_box_program() noexcept { return detail::temporal_thin_box_words; }
} // namespace x3m::renderer
