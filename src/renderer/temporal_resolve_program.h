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
} // namespace x3m::renderer
