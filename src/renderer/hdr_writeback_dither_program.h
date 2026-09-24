#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/hdr-writeback-dither-program.json.
inline constexpr std::uint32_t hdr_writeback_dither_words[] = {
#include "hdr_writeback_dither_program_inc.h"
};
}
// The identity write-back with the static +-0.5 code display dither
// (src/temporal/hdr_writeback_dither_ps.hlsl): ps_3_0, s0 = the FP16 scene
// texture sampled at TEXCOORD0, VPOS the pattern's pixel index, COLOR0 = the
// saturated sample plus the dither (alpha unchanged). No constants. Storage is
// immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& hdr_writeback_dither_program() noexcept {
    return detail::hdr_writeback_dither_words;
}
} // namespace x3m::renderer
