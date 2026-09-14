#pragma once
#include "linear_material.h"

namespace x3m::renderer {
// Six exact Asteroid contracts only. Native outputs remain
// intact; the VS also exports the linear COLOR1 varying, and the PS exports
// linear RGB/native source alpha to oC1 plus conservative positive oC2
// coverage. No temporal outputs are emitted. Config/atomic-failure/original-input contracts match
// linear_material.h. Stage recognition is independent (six VS / four PS);
// callers must enforce the six legal pairs rather than arbitrary cross-pairs.
// Cached at registration/binding, never at a draw. Unknown/cross-pair returns0.
std::uint32_t linear_distance_fade_sampler_mask(std::uint64_t vs, std::uint64_t ps) noexcept;
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
