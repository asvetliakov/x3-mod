#pragma once
#include <array>
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
    static constexpr unsigned rgb_usage = 10; // D3DDECLUSAGE_COLOR
    static constexpr unsigned rgb_usage_index = 1;
};
// BUMPMAP keeps its original TEX0-4 basis; temporal TEX5/6 remain unchanged.
struct LinearBumpMaterialAbi {
    static constexpr unsigned vertex_definition_base = 248;
    static constexpr unsigned pixel_definition_base = 212;
    static constexpr unsigned vertex_rgb_output = 9;
    static constexpr unsigned pixel_rgb_input = 8;
    static constexpr unsigned rgb_usage = 10; // D3DDECLUSAGE_COLOR
    static constexpr unsigned rgb_usage_index = 1;
};
bool linear_material_config_valid(const LinearMaterialConfig& config) noexcept;
// A native scalar changes only its semantic/component carrier. The live
// transaction must copy the original wrap bit to the destination component,
// preserving other bits and restoring application state after the draw.
struct LinearMaterialScalarTransport {
    std::uint8_t source_texcoord = 0, source_component = 0;
    std::uint8_t destination_texcoord = 0, destination_component = 0;
};
struct LinearMaterialPairContract {
    std::uint32_t sampler_mask = 0;
    bool bump = false; // Includes BUMPMAP_LOW; independent of sampler count.
    std::array<LinearMaterialScalarTransport,2> scalar_transport{};
    std::uint8_t scalar_transport_count = 0;
};
// One exact-pair lookup supplies both cached sampler admission and technique
// telemetry. A zero mask is unsupported; technique alone never admits a draw.
LinearMaterialPairContract linear_material_pair_contract(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// Exact 168 DEFAULT/BUMPMAP/BUMPMAP_LOW pairs, independent of the temporal registry. The
// live caller must also establish both combined objects, opaque scene coverage,
// gamma-2.2 composition, sampler decode state, no MSAA and the existing temporal
// gates. This helper establishes none of those draw-time conditions.
// Hull DEFAULT returns 0x0f, hull BUMPMAP/LOW 0x1f; Asteroid DEFAULT/BUMP
// return 0x07/0x0f; glass DEFAULT returns 0x07; XT DEFAULT/BUMP return 0x1d/0x39, and unsupported pairs zero. The mask only
// identifies required disabled-sRGB samplers; it establishes no dynamic gates.
std::uint32_t linear_material_sampler_mask(std::uint64_t vertex, std::uint64_t pixel) noexcept;
bool linear_material_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// True for the Asteroid-family pairs of the same tables: the reviewed pairs
// whose pixel row carries an asteroid base/detail layout, which includes the
// six distance-fade pairs of linear_distance_fade.h. Identity only: it admits
// nothing and reads no draw state. Diagnostic classification (shimmer trace).
bool linear_material_asteroid_pair(std::uint64_t vertex, std::uint64_t pixel) noexcept;

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
// Four XT DEFAULT programs require an explicitly authored producer repair.
// Ordinary and linear repaired pairs must be published together by the caller;
// these APIs never make the shared original VS a stage-global replacement.
bool linear_material_xt_default_pair(std::uint64_t vertex, std::uint64_t pixel) noexcept;
LinearMaterialResult linear_material_xt_default_vertex_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true, bool linear = true) noexcept;
LinearMaterialResult linear_material_xt_default_pixel_variant(const std::uint32_t* original,
    std::size_t words, const LinearMaterialConfig& config,
    std::vector<std::uint32_t>& output, bool current_depth = true, bool linear = true) noexcept;
} // namespace x3m::renderer
