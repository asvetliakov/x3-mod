#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/hdr-writeback-program.json.
inline constexpr std::uint32_t hdr_writeback_words[] = {
#include "hdr_writeback_program_inc.h"
};
}
// Stage-1 identity tonemap of the FP16 HDR scene path (src/temporal/
// hdr_writeback_ps.hlsl): ps_3_0, s0 = the FP16 scene texture sampled at
// TEXCOORD0, COLOR0 = the sample unchanged (alpha included). Storage is
// immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& hdr_writeback_program() noexcept {
    return detail::hdr_writeback_words;
}
} // namespace x3m::renderer
