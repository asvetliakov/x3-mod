#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "motion_output_profiles.h"

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

// The reviewed original pairs are the rows of the generated profile table. The
// live route substitutes variants only when the bound VS and PS fingerprints
// appear together in one row; a VS alias shared with other materials never
// qualifies on its own. Every row's constant bases equal the public ABI above
// (checked in material_motion.cpp), so the route uploads the same ranges for
// every pair.
inline constexpr const auto& material_motion_reviewed_pairs = motion_output_profiles;
// The row for an exact pair of original fingerprints, or nullptr.
const MotionOutputProfile* material_motion_profile(std::uint64_t vertex, std::uint64_t pixel) noexcept;
bool material_motion_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept;
// FNV-1a 64 over the program bytes in little-endian DWORD order. Equal to the
// proxy's byte hash of the same bytecode, so create-time hashes identify pairs.
std::uint64_t material_motion_fingerprint(const std::uint32_t* words, std::size_t count) noexcept;

// Table-driven transformer for the reviewed SM3 material pairs (classes A and
// B). All original position and color instructions remain unchanged. A
// previous-clip interpolator feeds our existing RGBA32F previous-UV/depth/
// validity program in the row's output register (oC1 for every row).
//
// These pure, creation-time transformers perform no D3D calls or per-draw work.
// The vertex and pixel programs are created separately by the application, so
// each stage transforms independently: the program is matched to a table row
// by exact fingerprint, length and version, and the row's offsets and register
// choices are then revalidated against the actual words (instruction framing,
// header boundaries, the four position dots, and that the chosen registers are
// unused by the original). Success replaces the owned output; failure leaves
// it intact. Input may alias the output vector. The caller must separately
// establish opaque scene coverage, compatible MRT state, the row's light-loop
// bound, valid history, and ownership/restoration of the reserved constants
// and render target. The current pixel ABI requires a zero-origin viewport and
// the existing jitter convention. A matching shader pair alone establishes
// none of these contracts.
MaterialMotionResult material_motion_vertex_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, std::vector<std::uint32_t>& output) noexcept;
MaterialMotionResult material_motion_pixel_variant(const std::uint32_t* pixel,
    std::size_t pixel_words, std::vector<std::uint32_t>& output) noexcept;
// Pair form retained for the detached fixtures: both stages are qualified
// against one row before either transforms, and success publishes both
// programs atomically. Input may alias either output vector.
MaterialMotionResult material_motion_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words,
    MaterialMotionVariant& output) noexcept;

// Row-explicit forms. The program must still carry the row's exact fingerprint,
// length and version (UnsupportedShader otherwise); the row's structural fields
// are then revalidated against the words (ProfileMismatch on any inconsistency).
// The table lookups above call these; the structural fixture uses them with
// deliberately perturbed rows to prove the revalidation refuses, which the
// fingerprint gate makes unreachable through the lookup forms.
MaterialMotionResult material_motion_vertex_variant_for(const MotionOutputProfile& row,
    const std::uint32_t* vertex, std::size_t vertex_words, std::vector<std::uint32_t>& output) noexcept;
MaterialMotionResult material_motion_pixel_variant_for(const MotionOutputProfile& row,
    const std::uint32_t* pixel, std::size_t pixel_words, std::vector<std::uint32_t>& output) noexcept;
} // namespace x3m::renderer
