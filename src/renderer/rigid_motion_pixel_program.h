#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/rigid-motion-pixel-program.json.
inline constexpr std::uint32_t rigid_motion_pixel_words[] = {
#include "rigid_motion_pixel_program_inc.h"
};
}
// Fixed ps_3_0 ABI: c0 inverse dimensions/prior jitter, c1 invalid/write mode;
// TEXCOORD0 previous homogeneous position -> RGBA32F UV/depth/valid sentinel.
// Storage is immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& rigid_motion_pixel_program() noexcept {
    return detail::rigid_motion_pixel_words;
}
} // namespace x3m::renderer
