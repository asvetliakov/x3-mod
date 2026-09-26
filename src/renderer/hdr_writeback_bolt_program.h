#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/hdr-writeback-bolt-program.json.
inline constexpr std::uint32_t hdr_writeback_bolt_words[] = {
#include "hdr_writeback_bolt_program_inc.h"
};
}
// The identity write-back with the bolt composite and no dither
// (src/temporal/hdr_writeback_bolt_ps.hlsl; docs/architecture/bolts-through-taa.md):
// ps_3_0, s0 = the resolved FP16 image, s2 = the pre-resolve FP16 scene, c29.x = W;
// COLOR0 = the sample, or at a flagged texel (alpha <= -1) the scene texel blended
// in at W * share with the scene's alpha. Storage is immutable, process-lifetime,
// allocation-free; extent includes END.
inline constexpr const auto& hdr_writeback_bolt_program() noexcept {
    return detail::hdr_writeback_bolt_words;
}
} // namespace x3m::renderer
