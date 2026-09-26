#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace x3m::renderer {
// A reviewed complete-program fingerprint plus all mutually exclusive clamp
// sites. Offsets count DWORDs from the shader version token, not byte offsets.
struct RadianceSite {
    std::size_t offset = 0;
    std::uint32_t destination = 0;
    std::uint32_t source = 0;
};
struct RadianceProfile {
    const char* name = nullptr;
    std::uint64_t fnv = 0;
    std::size_t word_count = 0;
    std::array<RadianceSite, 2> sites{};
    unsigned site_count = 0;
    std::size_t zero_def_offset = 0;
    unsigned zero_component = 0;
};
enum class RadianceResult {
    Applied,
    UnsupportedShader,
    InvalidInput,
    ProfileMismatch,
    InvalidProfile,
    AllocationFailure
};

// No D3D calls, global mutation or game assets. Build an owned shader variant
// removing only the reviewed vertex-radiance upper clamp; preserve the lower
// bound, RGB write mask, partial precision and every unrelated instruction.
// Success replaces output atomically. Failure leaves output unchanged; callers
// must use it only after Applied. Source/output aliasing is supported.
// The known-profile entry point rejects unrecognized shaders without patching.
RadianceResult material_radiance_variant(const std::uint32_t* source, std::size_t word_count,
                                         std::vector<std::uint32_t>& output) noexcept;

// Explicit reviewed profiles also support original synthetic verification
// shaders. This validates structural constraints, not arbitrary shader semantics;
// profile authors must verify that COLOR0 carries radiance at every named site.
RadianceResult apply_radiance_profile(const RadianceProfile& profile, const std::uint32_t* source,
                                      std::size_t word_count, std::vector<std::uint32_t>& output) noexcept;
std::uint64_t shader_fingerprint(const std::uint32_t* source, std::size_t word_count) noexcept;
const RadianceProfile* material_radiance_profiles(std::size_t* count) noexcept;
} // namespace x3m::renderer
