#pragma once
#include <cstddef>
#include <cstdint>

namespace x3m::renderer {
// Complete reviewed installed-archive maxima, including comments and END.
inline constexpr std::size_t kMaxReviewedVertexShaderWords = 769;
inline constexpr std::size_t kMaxReviewedPixelShaderWords = 1883;

struct RigidPositionProfile {
    std::uint64_t hash;
    std::uint32_t word_count;
    std::uint16_t matrix_register;
    bool named_world_view_projection; // CTAB hint only, not an eligibility gate.
};
// Whole-program FNV-1a/length whitelist from independently reviewed shader bytes.
// A match establishes the reviewed shader's POSITION.xyz / forced W=1 row-dot
// path, not object lifetime, input declaration, opaque coverage or stable buffers.
// Caller supplies a readable complete shader DWORD span; no COM calls/allocations.
const RigidPositionProfile* find_rigid_position(const std::uint32_t* words,
                                              std::size_t word_count) noexcept;

// Every reviewed archive VS is classified, but only HomogeneousRowDots is
// returned by find_rigid_position. These categories are routing facts, never
// automatic jitter, motion, opaque-pass, or history eligibility.
enum class VertexPositionPath : std::uint8_t {
    Unknown = 0,
    HomogeneousRowDots = 1,
    DirectClipXYZW = 2,
    DirectClipXYZWOne = 3,
    ViewXYBillboardProjection = 4
};
VertexPositionPath classify_vertex_position(const std::uint32_t* words,
                                            std::size_t word_count) noexcept;

struct PixelCoverageProfile {
    std::uint64_t hash;
    std::uint32_t word_count;
};
// Positive exact-profile proof of no shader discard/texkill or explicit/legacy
// depth output. Null means unproved, not necessarily unsafe. Render-state alpha
// testing, blending, stencil, MSAA, coverage and all replay gates remain external.
const PixelCoverageProfile* find_pixel_coverage(const std::uint32_t* words,
                                              std::size_t word_count) noexcept;
} // namespace x3m::renderer
