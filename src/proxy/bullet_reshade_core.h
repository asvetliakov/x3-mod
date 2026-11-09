#pragma once
#include "weapon_projectile_family.h"
#include <cstddef>
#include <cstdint>

// The bullet re-shade (X3M_WEAPON_EFFECTS=reshade; docs/reverse-engineering/fl/bullet-draw-batch.md section 6): which
// game pixel programs it replaces and the constants it uploads. Portable and allocation-free; model() is the native
// fixture's reference for the compiled program (weapon_effects_fixture.cpp reshade_cases, `reshade_program_matches_model`).
namespace x3m::weapon_effects::reshade {
// The INSTANCE_BULLETS pixel programs, the proxy's FNV-1a 64 of the bytes the game passes to CreatePixelShader
// (comments included): the B1 body `ps_1_1; tex t0; mul r0.rgb, t0, v0.a; +mov r0.a, t0.a` under the three compilers
// of the effects (row 19, `effects`/`effects2s`/`engine`/`engine2s`), effects_0000/0001 (row 18) and
// engine_0000/0001 (row 17) of effects-engine-remaining-emission.md; FL ships the AP bytes (offline scan of the FL
// catalogues, root + addon2, 2026-11-09). The batch submit binds `effects` (row 19) unless the batch carries its own
// material effect.
constexpr std::uint64_t bullet_programs[3] = {0xec1f5c4a2f4e1445ull, 0xd4a26efb7c603931ull, 0x84d3de8887c963c5ull};
constexpr bool bullet_program(std::uint64_t hash) noexcept {
    return hash && (hash == bullet_programs[0] || hash == bullet_programs[1] || hash == bullet_programs[2]);
}
// The program's constant register (weapon_bullet_reshade_ps.hlsl `reshade : register(c31)`): ps_2_0 has c0..c31; the
// game's bullet program reads none, and the register is put back after each substituted draw.
constexpr unsigned constant_register = 31;
// c31.w: the luminance normaliser, per texture (texture_scale). For the IRE's texture it is 1 / the IRE strip's peak
// texel luminance (0.36 of fx_bullets_diff.tga's body and head rows, section 6), so the smoothstep and the core work on
// 0..1 for the IRE (FL 64 A accepted the look at it); a draw whose bound texture has no table entry keeps it.
constexpr float luminance_scale = 2.75f;
// Per texture: c31.w = luminance_reference / the texture's p99 luminance (tables.bullet_textures, every level-0
// texel), anchored so fx_bullets_diff (p99 0.6169 in the FL table of 2026-11-09: IRE, PAC, HEPT and the flak
// fragments) keeps the accepted 2.75 and a brighter texture (fx_bullets2_diff, p99 0.9888: the PPC and 12 more rows)
// gets a proportionally smaller one. The p99, not the peak, so one hot texel does not set it.
constexpr float ire_texture_p99 = 0.6169f;
constexpr float luminance_reference = luminance_scale * ire_texture_p99;
inline float texture_scale(std::uint16_t p99_q) noexcept {
    return p99_q ? luminance_reference * 65535.f / float(p99_q) : luminance_scale;
}
// The texture keys (tools/effects/weapon_effects_table.py): FNV-1a 64 of the first head_bytes bytes and of all of them.
constexpr std::size_t head_bytes = 4096;
inline std::uint64_t fnv1a64(const unsigned char* p, std::size_t n) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
// The table entry the bytes the engine handed D3DXCreateTextureFromFileInMemoryEx are, or -1: a byte count compare
// per entry first (the common path ends there), then the 4096-byte head hash, then the whole hash, each computed at
// most once.
inline int texture_key(const core::Table& t, const void* data, std::size_t size) noexcept {
    if (!data || !size || !t.bullet_textures) return -1;
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t head = 0, hash = 0;
    bool head_done = false, hash_done = false;
    const unsigned n = t.bullet_textures < core::bullet_texture_capacity ? t.bullet_textures
                                                                         : core::bullet_texture_capacity;
    for (unsigned i = 0; i < n; ++i) {
        if (t.bullet_texture_bytes[i] != size) continue;
        if (!head_done) {
            head = fnv1a64(p, size < head_bytes ? size : head_bytes);
            head_done = true;
        }
        if (t.bullet_texture_head[i] != head) continue;
        if (!hash_done) {
            hash = fnv1a64(p, size);
            hash_done = true;
        }
        if (t.bullet_texture_hash[i] == hash) return int(i);
    }
    return -1;
}
// c31 from the family's tune (rs_gain, rs_edge, rs_core; clamped by the tune's bounds), w the IRE's normaliser (the
// device sequence uploads the draw's texture's instead, Plan::scale).
inline void constants(const projectile_family::Tune& t, float out[4]) noexcept {
    namespace pf = projectile_family;
    const auto clamp = [](float v, unsigned k) {
        return !(v >= pf::tune_min[k]) ? pf::tune_min[k] : v > pf::tune_max[k] ? pf::tune_max[k] : v;
    };
    out[0] = clamp(t.rs_gain, 14);
    out[1] = clamp(t.rs_edge, 15);
    out[2] = clamp(t.rs_core, 16);
    out[3] = luminance_scale;
}
static_assert(projectile_family::tune_keys >= 17 &&
                  projectile_family::key_is(projectile_family::tune_key_names[14], "rs_gain") &&
                  projectile_family::key_is(projectile_family::tune_key_names[15], "rs_edge") &&
                  projectile_family::key_is(projectile_family::tune_key_names[16], "rs_core"),
              "the re-shade keys' indices");
// The CPU model of the program (the host test's reference; the fixture's readback checks it): radiance of one sample
// t (RGB 0..1) at vertex alpha a.
inline void model(const float c[4], const float t[3], float a, float out[3]) noexcept {
    float l = (0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2]) * c[3];
    l = l < 0.f ? 0.f : l > 1.f ? 1.f : l;
    float x = c[1] > 0.f ? l / c[1] : 1.f;
    x = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
    const float body = c[0] * x * x * (3.f - 2.f * x);
    float peak = t[0] > t[1] ? t[0] : t[1];
    peak = peak > t[2] ? peak : t[2];
    peak = peak > 1.f / 256.f ? peak : 1.f / 256.f;
    const float core = c[2] * l * l * l * l;
    for (unsigned i = 0; i < 3; ++i) out[i] = (body * t[i] + core * (0.65f + 0.35f * t[i] / peak)) * a;
}
} // namespace x3m::weapon_effects::reshade
