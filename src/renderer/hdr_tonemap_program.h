#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/hdr-tonemap-program.json.
inline constexpr std::uint32_t hdr_tonemap_words[] = {
#include "hdr_tonemap_program_inc.h"
};
}
// Stage-2 AgX tonemap of the FP16 HDR scene path (src/temporal/agx.hlsl,
// docs/architecture/hdr-scene-path.md section 3): ps_3_0, s0 = the FP16 scene
// texture sampled at TEXCOORD0, c8..c21 = x3::temporal::AgxConstants
// (exposure, clamp, decode mode, matrices, log range, sigmoid, look),
// COLOR0 = display-encoded RGB with the scene alpha carried. Storage is
// immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& hdr_tonemap_program() noexcept {
    return detail::hdr_tonemap_words;
}
} // namespace x3m::renderer
