#pragma once
#include "weapon_projectile_family.h"
#include <cstdint>

// The muzzle flash re-shade (X3M_WEAPON_EFFECTS=reshade, FL; docs/architecture/muzzle-flash-look.md,
// docs/reverse-engineering/fl/muzzle-flash-draw.md): which draws it takes, the constants it uploads and the CPU model of
// its program (src/effects/weapon_muzzle_reshade_ps.hlsl). Portable and allocation-free; the native fixture checks the
// compiled program against model() (weapon_effects_fixture.cpp muzzle_reshade_cases).
namespace x3m::weapon_effects::muzzle_reshade {
// The flash draws with the FL standard_lighting(2s) DEFAULT pair; its vertex shader (FNV-1a 64 of the bytes the game
// passes to CreateVertexShader) is the one partner of every standard_lighting DEFAULT pixel program (FL root + addon2,
// verification/results/muzzle-reshade/muzzle_inputs_out.txt). Our ps_3_0 program reads that VS's COLOR0 (alpha),
// TEXCOORD0 (centroid UV) and TEXCOORD1 (camera minus the world position: `o3 = c34..36.w - g_mWorld x pos`), and the
// draw's world origin, scale and camera come from the same VS's constants (g_mWorld c28..c30, g_mViewInverse c34..c36),
// so this hash is the ABI gate, not the identity: the identity is the node's body id.
constexpr std::uint64_t vertex_shader = 0x494fe349b8bc12ecull;
constexpr unsigned world_register = 28, view_inverse_register = 34; // the VS's g_mWorld rows and g_mViewInverse rows
constexpr unsigned vs_rows_first = 28, vs_rows_count = 9;            // c28..c36: the block the shadow keeps
constexpr std::uint16_t vs_rows_read = 0x1c7u; // the shadow's known bits draw_constants needs: c28..c30 and c34..c36
// FL node +0x144: the body id the object-trace scope's node carries at the material submit (muzzle-flash-draw.md 1).
constexpr std::uint32_t node_body_offset = 0x144;
// The muzzle bodies. TODO (ledger, "Muzzle flash re-shade on FL"): derive this list from weapon_effects.json's muzzle
// rows through Effects -> CUT scene -> part bodies once the table generator resolves cut scenes
// (tools/effects/weapon_effects_census.py stops at the scene id); today body 445 alone (scene 04251: Effects rows 22,
// 157, 52, 190, 195, 202, 26, 54).
struct Body {
    std::uint32_t id;
    float lit_radius; // vanilla's lit extent from the gun, model units (90 % of the emitted energy, muzzle_inputs_out.txt)
    float mean;       // the cell's centre luminance (the texel modulator's reference)
    // The fan's overlap at the gun (every quad adds its share there), linear in x = cos^2 of the angle between the view
    // ray and the flash axis: N(x) = overlap[0] + overlap[1] x. Gain and core are divided by N per draw, so the keys are
    // the luminance the whole fan sums from every view direction.
    float overlap[2];
    float hue[3]; // luminance-weighted hue of the lit texels, scaled to luminance 1
};
// Body 445 / tex/true/384 top-left cell (muzzle_inputs_out.txt). Overlap: fitted to the rendered real mesh
// (overlap_by_angle_out.txt, the dump's DUMP_MUZZLE_SWEEP rows, yaw 0..90 degrees at 30 m and the scale peak): the
// re-shaded energy times the draw's N, scaled to 9.14 at the 25-degree calibration pose; 8.89 side-on to 10.20
// axis-on, worst 1.8 %. The ray sum through the gun (the same file's first part: 5.9 side-on, 27.3 axis-on) does not
// describe the image: with a constant 9.14 the rendered peak stays 1.37..1.58 and the white radius 7.3..8.6 px from
// side-on to axis-on, the energy ratio 0.133..0.158.
constexpr Body bodies[] = {
    {445u, 45618.f, 0.166f, {8.8915f, 1.3111f}, {1.7507f, 0.8485f, 0.2903f}},
};
constexpr unsigned body_count = sizeof bodies / sizeof bodies[0];
// The body's row, or body_count. One compare per listed body.
constexpr unsigned body_index(std::uint32_t id) noexcept {
    unsigned i = 0;
    while (i < body_count && bodies[i].id != id) ++i;
    return i;
}
// The program's constant block: c192..c195 (ps_3_0 has c0..c223; the game's standard_lighting programs read c0..c13,
// the proxy's own variants keep their literals in local DEFs). Written per substituted draw, put back after it.
constexpr unsigned constant_register = 192, constant_count = 4;
// The four keys (the dev tuning file's mz_*, weapon_projectile_family.h), clamped by the tune's bounds.
using projectile_family::clamp_key;
// c193..c195 for one body and the family's tune (mz_size, mz_core, mz_gain, mz_smooth; clamped by the tune's bounds);
// c192 and c194.xy are the draw's (draw_constants). The size scales the per-draw radius, kept in c193.z; the keys gain
// and core stay in c194.zw (the program does not read them) for draw_constants to divide by the draw's overlap:
//   c192 = (camera - origin (world), 1 / (lit radius x world scale x size))       per draw
//   c193 = (1 - smooth, 1 / mean, size, 0)
//   c194 = (gain / N, core / N, gain, core), N the body's overlap at the draw's view angle   xy per draw
//   c195 = (hue, 0)
inline void constants(const Body& b, const projectile_family::Tune& t, float out[16]) noexcept {
    out[0] = out[1] = out[2] = 0.f;
    // No draw yet: c192.w 0 and c194.xy 0 draw black, should the block ever be uploaded before draw_constants (it is
    // not: begin_muzzle_reshade refuses the draw when draw_constants does).
    out[3] = 0.f;
    out[4] = 1.f - clamp_key(t.mz_smooth, projectile_family::key_mz_smooth);
    out[5] = 1.f / b.mean;
    out[6] = clamp_key(t.mz_size, projectile_family::key_mz_size);
    out[7] = 0.f;
    out[8] = out[9] = 0.f;
    out[10] = clamp_key(t.mz_gain, projectile_family::key_mz_gain);
    out[11] = clamp_key(t.mz_core, projectile_family::key_mz_core);
    out[12] = b.hue[0];
    out[13] = b.hue[1];
    out[14] = b.hue[2];
    out[15] = 0.f;
}
// The body's overlap at x = cos^2 of the view ray's angle to the flash axis (x clamped to [0, 1], NaN -> 0).
inline float overlap_at(const Body& b, float x) noexcept {
    x = !(x >= 0.f) ? 0.f : x > 1.f ? 1.f : x;
    return b.overlap[0] + x * b.overlap[1];
}
// c192 and c194.xy from the draw's VS rows c28..c36 (g_mWorld rows c28..c30: the origin in .w, the model z axis, the
// flash axis, in column 2; g_mViewInverse rows c34..c36: the camera in .w): the view vector v = camera - origin, the
// radius from the z column's length, and the overlap N at cos^2 = (v . z)^2 / (|v|^2 |z|^2) (a camera at the gun counts
// as side-on). false (nothing written) when the scale is not a finite positive number: the caller leaves the draw to
// the route it would have taken without the re-shade. Reads c194.zw (the keys), writes c194.xy, so it may run again on
// the same block.
inline bool draw_constants(const Body& b, const float rows[36], float out[16]) noexcept {
    const float* w = rows;      // c28, c29, c30
    const float* v = rows + 24; // c34, c35, c36
    const float zx = w[2], zy = w[6], zz = w[10];
    const float s2 = zx * zx + zy * zy + zz * zz;
    if (!(s2 > 1e-24f && s2 < 1e24f)) return false;
    float s = s2; // Newton square root: no libm in a header the hook fixtures compile
    for (unsigned i = 0; i < 40; ++i) s = 0.5f * (s + s2 / s);
    const float radius = b.lit_radius * s * out[6];
    if (!(radius > 0.f && radius < 1e30f)) return false;
    const float vx = v[3] - w[3], vy = v[7] - w[7], vz = v[11] - w[11];
    const float v2 = vx * vx + vy * vy + vz * vz, dot = vx * zx + vy * zy + vz * zz;
    const float c2 = v2 > 0.f && v2 < 1e30f ? dot * dot / (v2 * s2) : 0.f;
    const float n = 1.f / overlap_at(b, c2);
    out[0] = vx;
    out[1] = vy;
    out[2] = vz;
    out[3] = 1.f / radius;
    out[8] = out[10] * n;
    out[9] = out[11] * n;
    return true;
}
// The CPU model of the program (the fixture's reference): the colour of one sample. t: the diffuse texel (RGBA 0..1),
// view: TEXCOORD1 (camera minus the world position), colour_a: COLOR0.a, light_a: the light-map texel's alpha, glow: c4.x.
inline void model(const float c[16], const float t[4], const float view[3], float colour_a, float light_a, float glow,
                  float out[4]) noexcept {
    const float dx = (c[0] - view[0]) * c[3], dy = (c[1] - view[1]) * c[3], dz = (c[2] - view[2]) * c[3];
    const float d2 = dx * dx + dy * dy + dz * dz;
    const float k = 1.f - (d2 < 1.f ? d2 : 1.f);
    const float g = k * k, g4 = g * g * g * g;
    const float lum = 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2];
    const float mod = 1.f + (lum * c[5] - 1.f) * c[4];
    const float body = c[8] * g * mod, core = c[9] * g4;
    for (unsigned i = 0; i < 3; ++i) out[i] = body * c[12 + i] + core * (0.5f * c[12 + i] + 0.5f);
    out[3] = (t[3] + (light_a - t[3]) * glow) * colour_a;
}
} // namespace x3m::weapon_effects::muzzle_reshade
