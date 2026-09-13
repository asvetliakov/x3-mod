#pragma once
#include "linear_material.h"

namespace x3m::renderer {
// Detached prototype. Six exact Asteroid contracts only. Native outputs remain
// intact; the VS also exports the linear COLOR1 varying, and the PS exports
// linear RGB/native source alpha to oC1 plus conservative positive oC2
// coverage. No temporal outputs, runtime registration or draw admission are
// provided. Config/atomic-failure/original-input contracts match
// linear_material.h. Stage recognition is independent (six VS / four PS);
// callers must enforce the six legal pairs rather than arbitrary cross-pairs.
LinearMaterialResult linear_distance_fade_vertex_variant(
    const std::uint32_t *original, std::size_t words,
    const LinearMaterialConfig &config,
    std::vector<std::uint32_t> &output) noexcept;
LinearMaterialResult
linear_distance_fade_pixel_variant(const std::uint32_t *original,
                                   std::size_t words,
                                   const LinearMaterialConfig &config,
                                   std::vector<std::uint32_t> &output) noexcept;
} // namespace x3m::renderer
