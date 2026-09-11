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

// One reviewed original pair. The live route substitutes variants only when the
// bound VS and PS fingerprints appear together in one reviewed entry; a VS
// alias shared with other materials never qualifies on its own.
struct MaterialMotionPair { std::uint64_t vertex = 0, pixel = 0; };
constexpr MaterialMotionPair material_motion_reviewed_pairs[] = {
    {0x53a0a641107ed76cull, 0x8759c7838bbc86c2ull}};
bool material_motion_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// FNV-1a 64 over the program bytes in little-endian DWORD order. Equal to the
// proxy's byte hash of the same bytecode, so create-time hashes identify pairs.
std::uint64_t material_motion_fingerprint(const std::uint32_t* words, std::size_t count) noexcept;

// Transformer for one reviewed SM3 material pair. All original position and
// color instructions remain unchanged. A previous-clip interpolator feeds our
// existing RGBA32F previous-UV/depth/validity program in COLOR1.
//
// These pure, creation-time transformers perform no D3D calls or per-draw work.
// The vertex and pixel programs are created separately by the application, so
// each stage transforms independently; each accepts only its exact reviewed
// program. Success replaces the owned output; failure leaves it intact. Input
// may alias the output vector. The caller must separately establish opaque
// scene coverage, compatible MRT state, light count 0..8, valid history, and
// ownership/restoration of the reserved constants and render target.
// The current pixel ABI requires a zero-origin viewport and the existing jitter
// convention. A matching shader pair alone establishes none of these contracts.
MaterialMotionResult material_motion_vertex_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, std::vector<std::uint32_t>& output) noexcept;
MaterialMotionResult material_motion_pixel_variant(const std::uint32_t* pixel,
    std::size_t pixel_words, std::vector<std::uint32_t>& output) noexcept;
// Pair form retained for the detached fixtures: both stages are qualified
// before either transforms, and success publishes both programs atomically.
// Input may alias either output vector.
MaterialMotionResult material_motion_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words,
    MaterialMotionVariant& output) noexcept;
} // namespace x3m::renderer
