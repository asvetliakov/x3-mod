#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/taa-sharpen-program.json.
inline constexpr std::uint32_t taa_sharpen_words[] = {
#include "taa_sharpen_program_inc.h"
};
}
// Post-resolve sharpen of the 8-bit route and the HDR identity write-back
// (src/temporal/taa_sharpen_ps.hlsl with rcas.hlsl; docs/architecture/
// temporal-integration.md, "Post-resolve sharpen"): ps_3_0, s0 = the resolved
// FP16 image (display-referred values) sampled point/clamp at TEXCOORD0 and
// its four cross neighbours, c23 = x3::temporal::SharpenConstants (gain,
// 1/width, 1/height), COLOR0 = RCAS of the five taps with the centre alpha
// carried. Storage is immutable, process-lifetime, allocation-free; extent
// includes END.
inline constexpr const auto& taa_sharpen_program() noexcept {
    return detail::taa_sharpen_words;
}
} // namespace x3m::renderer
