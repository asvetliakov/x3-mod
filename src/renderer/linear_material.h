#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
// Process-start gains embedded in shader-local DEFs; changing a gain requires
// a new owned variant. Every gain must be finite and in [0,16].
struct LinearMaterialConfig {
    float direct_gain = 1.0f;
    float material_emissive_gain = 1.0f;
    float lightmap_emissive_gain = 1.0f;
};
enum class LinearMaterialResult {
    Applied, InvalidInput, InvalidConfig, UnsupportedShader, ProfileMismatch,
    ResourceLimit, AllocationFailure
};
struct LinearMaterialAbi {
    static constexpr unsigned vertex_definition_base = 248;
    static constexpr unsigned pixel_definition_base = 212;
    static constexpr unsigned vertex_rgb_output = 8;
    static constexpr unsigned pixel_rgb_input = 7;
    static constexpr unsigned rgb_texcoord = 6;
};
// BUMPMAP keeps its original TEX0-4 basis; temporal TEX5/6 remain unchanged.
struct LinearBumpMaterialAbi {
    static constexpr unsigned vertex_definition_base = 248;
    static constexpr unsigned pixel_definition_base = 212;
    static constexpr unsigned vertex_rgb_output = 9;
    static constexpr unsigned pixel_rgb_input = 8;
    static constexpr unsigned rgb_texcoord = 7;
};
bool linear_material_config_valid(const LinearMaterialConfig& config) noexcept;
// Exact thirty DEFAULT/BUMPMAP pairs, independent of the temporal registry. The
// live caller must also establish both combined objects, opaque scene coverage,
// gamma-2.2 composition, sampler decode state, no MSAA and the existing temporal
// gates. This helper establishes none of those draw-time conditions.
// DEFAULT returns 0x0f, BUMPMAP 0x1f, unsupported pairs zero. The mask only
// identifies required disabled-sRGB samplers; it establishes no dynamic gates.
std::uint32_t linear_material_sampler_mask(std::uint64_t vertex, std::uint64_t pixel) noexcept;
bool linear_material_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;

// Pure create-time combined material + same-draw motion/depth transformations.
// Inputs are complete ORIGINAL programs. Original fingerprint/count, material
// sites, resource reservations and the ordinary motion row must all validate.
// Motion's existing row-explicit API only receives the immutable original;
// original-offset merging then preserves its inserted temporal bytes exactly.
// New RGB uses full precision and a distinct varying; original sampled alpha,
// position, angular math and temporal outputs retain their existing operations.
// Failure leaves output intact; input may alias output. No D3D calls, constant
// uploads or per-draw work occur here. GPU and native-Windows behavior require
// separate qualification, including the documented DX9 exceptional-value rules.
LinearMaterialResult linear_material_vertex_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true) noexcept;
LinearMaterialResult linear_material_pixel_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true) noexcept;
} // namespace x3m::renderer
