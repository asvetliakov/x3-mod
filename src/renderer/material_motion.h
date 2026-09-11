#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
struct MaterialMotionVariant {
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> pixel;
};
enum class MaterialMotionResult {
    Applied, InvalidInput, UnsupportedShader, ProfileMismatch, AllocationFailure
};
struct MaterialMotionAbi {
    static constexpr unsigned previous_vertex_constant = 252; // Four submitted rows.
    static constexpr unsigned pixel_coordinates_constant = 216; // Inverse size, prior jitter UV.
    static constexpr unsigned pixel_mode_constant = 217; // x=1 valid history request, x=0 invalid.
    static constexpr unsigned motion_render_target = 1;
};

// Detached prototype for one reviewed SM3 material pair. All original position
// and color instructions remain unchanged. A previous-clip interpolator feeds
// our existing RGBA32F previous-UV/depth/validity program in COLOR1.
//
// This pure, creation-time transformer performs no D3D calls or per-draw work.
// Success atomically replaces both owned programs; failure leaves output intact.
// Input may alias either output vector. The caller must separately establish
// opaque scene coverage, compatible MRT state, light count 0..8, valid history,
// and ownership/restoration of the reserved constants and render target.
// The current pixel ABI requires a zero-origin viewport and the existing jitter
// convention. A matching shader pair alone establishes none of these contracts.
MaterialMotionResult material_motion_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words,
    MaterialMotionVariant& output) noexcept;
} // namespace x3m::renderer
