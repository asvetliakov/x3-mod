#pragma once
#include <cstdint>

// Screen-emission producers (docs/architecture/screen-emission-region.md):
// the vertex shaders whose non-indexed draws from a DISCARD-locked dynamic
// buffer get the locked-prefix bound (step B) and, in step C, the packed
// screen policy. Keyed by the proxy's FNV-1a 64 fingerprint of the original
// VS bytes (the sweep's vs_<hash>.bin name). Step C's admission shares this
// table; step B marks the buffer of an admitted draw as scannable.
namespace x3m::screen_emission {

// INSTANCE_BULLETS / INSTANCE: g_mViewProjection at c0-3 over world-space
// FLOAT3 positions, stride 24 (effects-engine-remaining-emission.md,
// "Bullet vertex buffer writer").
constexpr std::uint64_t vertex_shaders[] = {0x5e484a06672e28fbull};

inline bool admitted_vertex_shader(std::uint64_t hash) noexcept {
    if (!hash) return false;
    for (const auto known : vertex_shaders) if (known == hash) return true;
    return false;
}

} // namespace x3m::screen_emission
