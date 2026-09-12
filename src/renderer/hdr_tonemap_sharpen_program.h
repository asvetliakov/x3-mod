#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/hdr-tonemap-sharpen-program.json.
inline constexpr std::uint32_t hdr_tonemap_sharpen_words[] = {
#include "hdr_tonemap_sharpen_program_inc.h"
};
}
// AgX tonemap followed by the post-resolve sharpen (src/temporal/
// agx_sharpen_ps.hlsl including agx.hlsl and rcas.hlsl): ps_3_0, s0 = the
// resolved FP16 scene texture sampled at TEXCOORD0 and its four cross
// neighbours, c8..c21 = x3::temporal::AgxConstants (as hdr_tonemap_program),
// c23 = x3::temporal::SharpenConstants, COLOR0 = RCAS of the five
// display-encoded AgX outputs with the centre's scene alpha carried. Storage
// is immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& hdr_tonemap_sharpen_program() noexcept {
    return detail::hdr_tonemap_sharpen_words;
}
} // namespace x3m::renderer
