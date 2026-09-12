#pragma once
#include <cstdint>

namespace x3m::renderer {
namespace detail {
// Only our authored shader is embedded. The deterministic native compilation
// manifest is verification/results/current-depth-pixel-program.json.
inline constexpr std::uint32_t current_depth_pixel_words[] = {
#include "current_depth_pixel_program_inc.h"
};
}
// Fixed ps_3_0 fragment: TEXCOORD1 (current clip z in .x, w in .y) -> COLOR0
// = z/w replicated, i.e. device depth for an R32F target. No literal
// constants; one temporary. The transformer relocates the input, the
// temporary and the color output to the row's depth registers and oC2.
// Storage is immutable, process-lifetime, allocation-free; extent includes END.
inline constexpr const auto& current_depth_pixel_program() noexcept {
    return detail::current_depth_pixel_words;
}
} // namespace x3m::renderer
