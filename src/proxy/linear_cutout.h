#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// The first qualified cutout state. These are public D3D9 enum values; the
// adapter/static assertions in motion_output.cpp bind them to the SDK. No
// generic alpha-test allowlist or reconstructed shader alpha is involved.
namespace x3m::cutout {
// The qualified pairs as (vertex, pixel) in sequence. This is the single
// source: pair() scans it and the shader-population classifier
// (src/renderer/shader_population.h) enumerates it, so neither form can
// admit a program the other does not.
inline constexpr std::uint64_t pair_hashes[] = {
    0x53a0a641107ed76cull, 0x63f96eba9eea7880ull,
    0x4944d81dfe531b37ull, 0x5e0a10fe752b6140ull};
inline constexpr std::size_t pair_hash_count = sizeof pair_hashes / sizeof pair_hashes[0];
static_assert(pair_hash_count == 4, "two qualified cutout pairs");
constexpr bool pair(std::uint64_t vs, std::uint64_t ps) noexcept {
    for (std::size_t i = 0; i + 1 < pair_hash_count; i += 2)
        if (pair_hashes[i] == vs && pair_hashes[i + 1] == ps) return true;
    return false;
}

enum class Capability : std::uint8_t { Pending, Ready, Unsupported, Retry };
constexpr Capability query_result(std::int32_t hr) noexcept {
    return hr >= 0 ? Capability::Ready
        : static_cast<std::uint32_t>(hr) == 0x8876086au ? Capability::Unsupported
        : Capability::Retry;
}
// ALPHAFUNC, ALPHAREF, ZFUNC, FOGENABLE, DITHERENABLE, STENCILENABLE,
// CULLMODE, FILLMODE. Only these additional cached states belong to this arm.
inline constexpr std::array<std::uint32_t, 8> values{{7, 1, 4, 0, 0, 0, 1, 3}};
constexpr bool state(const std::array<std::uint32_t, 8>& actual) noexcept {
    for (unsigned i=0;i<values.size();++i) if (actual[i]!=values[i]) return false;
    return true;
}
// Successful native forwarding can have written foreground color without
// same-draw motion. Known no-color/NEVER draws and suppressed/failed submits do
// not poison the frame. The game's source-over cutout pass, observed exactly
// (ALPHABLENDENABLE on, SRCBLEND SRCALPHA, DESTBLEND INVSRCALPHA; independent
// of ZWRITEENABLE), is not a miss either: it takes the ordinary native colour
// path with camera reprojection only, the frame keeps its history. Any other
// or unknown blend factor stays a conservative miss. Unknown state is
// conservative, confined to an exact requested scene cutout; visibility is
// not guessed from object identity.
constexpr bool missed(bool candidate, bool submitted, bool success, bool routed,
                      bool test_known, std::uint32_t test,
                      bool color_known, std::uint32_t color,
                      bool alpha_known, std::uint32_t alpha,
                      bool z_known, std::uint32_t z,
                      bool zfunc_known, std::uint32_t zfunc,
                      bool source_over = false) noexcept {
    return candidate && submitted && success && !routed && (!test_known || test!=0)
        && (!color_known || (color & 7u)!=0) && (!alpha_known || alpha!=1)
        && (!z_known || !z || !zfunc_known || zfunc!=1)
        && !source_over;
}
// The observed source-over triple, all three states known.
constexpr bool source_over(bool blend_known, std::uint32_t blend, bool src_known, std::uint32_t src,
                           bool dst_known, std::uint32_t dst) noexcept {
    return blend_known && blend!=0 && src_known && src==5 && dst_known && dst==6; // D3DBLEND_SRCALPHA / D3DBLEND_INVSRCALPHA
}
// A valid supplemental fade mask cannot repair missing cutout correspondence.
constexpr bool unavailable(bool missed_cutout, bool composition_required,
                           bool composition_ready) noexcept {
    return missed_cutout || (composition_required && !composition_ready);
}
}
