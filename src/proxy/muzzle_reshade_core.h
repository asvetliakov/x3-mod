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
// The body id the object-trace scope's node carries at the material submit: FL node +0x144 (muzzle-flash-draw.md 1),
// AP +0x140 (ap-bullet-batch-site.md section 3: the scope 0x004c5228 -> 0x004c0150's node argument, read at 0x004c5008).
constexpr std::uint32_t node_body_offset = core::layout::node_body;
// The muzzle bodies: every classic standard_lighting muzzle flash body of the FL census (MATERIAL3/5, Materials flags
// DESTINATIONBLEND|2SIDED; verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census_out.txt): 445 (scene 04251:
// IRE, PD, PSG, PBC, flak, frags, the secondary of PPC/PSP/MAM/CIG), 446 (04253: EMP, Mining, IPG, IonC, IBL, SSC, the
// primary of PPC/PSP/MAM/CIG), 444 (04252: HEPT, PAL, Tri-Beam, Fusion Beam, EBC, Gauss), 450 (04260: PAC, Mass
// Driver), 447 (04258: FBL), 314 (04261: Ion Disruptor), 10463 (04437: PBE). Body 12009 (scene 08001, PRG) draws with
// effects.fx, not this pair: not listed. Most frequent first (a hit costs one compare per row before it). TODO (ledger,
// "Muzzle flash re-shade on FL"): derive this list from weapon_effects.json's muzzle rows through Effects -> CUT scene
// -> part bodies once the table generator resolves cut scenes (tools/effects/weapon_effects_census.py stops at the
// scene id).
struct Body {
    std::uint32_t id;
    float lit_radius; // vanilla's lit extent from the gun, model units (90 % of the emitted energy, muzzle_inputs_out.txt)
    // The texel modulator's reference (the grain): 445's cell centre luminance; for the others the body's own
    // glow-weighted texel luminance, so each keeps its texels' grain at full strength (muzzle_bodies.py)
    float mean;
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
// The other bodies (verification/results/muzzle-reshade/muzzle_bodies.py, muzzle_bodies_out.txt, 445's functions on
// each body's own mesh and atlas cell). The split: mean carries the grain, overlap the energy.
//   lit_radius, hue  measured as 445's.
//   mean             measured: the body's glow-weighted texel luminance at the baked size (445's radial fit assumes a
//                    round blob centred in its cell and fails on the strip cells of 444, 446, 447, 314 and 10463), so
//                    the glow-weighted modulator is 1 and the modulator at the brightest texel 1.03 (314) .. 19.1 (450)
//                    against 445's 3.75.
//   overlap          inferred, not rendered: 445's rendered fit x the body's ray sum through the gun over 445's (4.00 ..
//                    23.44 against 9.14) x 1 / 0.2547 (445's glow-weighted modulator at mz_smooth 0.1 over the body's
//                    1), so gain and core over N sum to 445's luminance at the gun at the baked smooth; at mz_smooth 1
//                    these bodies draw 3.93x dimmer than 445 relative to it, at 0 1.48x brighter.
constexpr Body bodies[] = {
    {445u, 45618.f, 0.166f, {8.8915f, 1.3111f}, {1.7507f, 0.8485f, 0.2903f}},     // tex/true/384, top-left cell
    {446u, 45865.f, 0.3702f, {89.5103f, 13.1988f}, {1.0760f, 0.9683f, 1.0905f}},  // tex/true/383
    {444u, 59102.f, 0.2187f, {15.2693f, 2.2515f}, {0.7865f, 1.1132f, 0.5072f}},   // tex/true/376
    {450u, 35567.f, 0.0473f, {28.6190f, 4.2200f}, {0.8500f, 1.0357f, 1.0883f}},   // tex/true/384, top-right cell
    {447u, 44959.f, 0.3804f, {89.4113f, 13.1842f}, {1.1154f, 0.9597f, 1.0595f}},  // tex/true/383
    {314u, 51481.f, 0.9699f, {30.9032f, 4.5568f}, {0.8720f, 1.0313f, 1.0672f}},   // tex/true/384, bottom cell
    {10463u, 63918.f, 0.0385f, {19.5086f, 2.8766f}, {1.3042f, 0.9372f, 0.7269f}}, // tex/true/380
    // AP Mayhem's flash bodies (addon/06.cat; docs/reverse-engineering/ap-bullet-batch-site.md section 3: scenes 08311,
    // 08002, 04437's override, 08004, 08005, 08006; DESTINATIONBLEND without 2SIDED, so the one-sided standard_lighting
    // DEFAULT pair), measured as the six above (muzzle_bodies.py --edition ap, muzzle_bodies_ap_out.txt: 445 first, its
    // row equal to the FL run's, so the overlap is normalised to it; overlap inferred). 12013 and 12014 have grey cells
    // (hue 1, 1, 1: a white flash). 12011 is MATERIAL6 effects.fx (like 12009): not this pair, not listed. Absent from the
    // FL and stock AP catalogues: there a miss costs one compare each, after the seven.
    {12012u, 35503.f, 0.0449f, {28.5714f, 4.2130f}, {1.1945f, 0.9475f, 0.9477f}}, // tex/true/924 (PALC, PAA)
    {12013u, 59040.f, 0.2072f, {15.2693f, 2.2515f}, {1.0000f, 1.0000f, 1.0000f}}, // tex/true/925 (PBE, PPC/IBL)
    {12014u, 63923.f, 0.0396f, {19.5086f, 2.8766f}, {1.0000f, 1.0000f, 1.0000f}}, // tex/true/926 (PPC/IBL)
    {12016u, 59102.f, 0.2178f, {15.2693f, 2.2515f}, {0.8801f, 1.1015f, 0.3481f}}, // tex/true/931 (EBC, FAA, AKE, BKE, GC)
    {12017u, 51167.f, 0.0373f, {37.5131f, 5.5315f}, {2.5455f, 0.5790f, 0.6198f}}, // tex/true/932 (PBG, ISR)
    {12018u, 49429.f, 0.0349f, {36.7517f, 5.4192f}, {1.0673f, 0.8098f, 2.6859f}}, // tex/true/933 (PRG, CFA, RL)
};
constexpr unsigned body_count = sizeof bodies / sizeof bodies[0];
static_assert(body_count < 256, "MotionRoute::muzzle_body is a byte");
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
