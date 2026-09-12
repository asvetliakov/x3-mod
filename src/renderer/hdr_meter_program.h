#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shaders are embedded. The deterministic native
// compilation manifests are verification/results/hdr-meter-level0-program.json
// and hdr-meter-reduce-program.json.
inline constexpr std::uint32_t hdr_meter_level0_words[] = {
#include "hdr_meter_level0_program_inc.h"
};
inline constexpr std::uint32_t hdr_meter_reduce_words[] = {
#include "hdr_meter_reduce_program_inc.h"
};
}
// Exposure meter of the FP16 HDR scene path, stage 2 (src/temporal/
// hdr_meter_level0_ps.hlsl, hdr_meter_reduce_ps.hlsl): ps_3_0 reduction
// chain, 4x per axis per draw. Level 0 folds the log2-luminance of the
// decoded scene into the first reduction (s0 = the FP16 scene, c0 source
// size, c1 decode mode, c2 meter floor/clip, c3 output size); reduce
// averages 16 taps of the previous R32F level (s0, c0, c3). Storage is
// immutable, process-lifetime, allocation-free; extents include END.
inline constexpr const auto& hdr_meter_level0_program() noexcept {
    return detail::hdr_meter_level0_words;
}
inline constexpr const auto& hdr_meter_reduce_program() noexcept {
    return detail::hdr_meter_reduce_words;
}
} // namespace x3m::renderer
