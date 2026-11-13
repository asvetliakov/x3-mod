#pragma once
#include "bullet_reshade_core.h" // lone_blend, the classic pixel pairs per edition, luminance_reference
#include "muzzle_reshade_core.h" // the muzzle bodies this route leaves to the muzzle re-shade
#include "weapon_projectile_family.h"
#include <cstdint>

// The hull hit re-shade (X3M_WEAPON_EFFECTS=reshade; docs/architecture/hull-hit-look.md, the facts in
// docs/reverse-engineering/fl/hull-hit-draw.md): which impact draws it takes, the constants it uploads and the CPU model
// of its two programs (src/effects/weapon_hull_reshade.hlsl through weapon_hull_reshade_effects_ps.hlsl, ps_2_0, and
// weapon_hull_reshade_classic_ps.hlsl, ps_3_0). Portable and allocation-free; MotionOutput (motion_output_hull_reshade_inc.h),
// the host test (verification/analysis/test_weapon_hull_reshade.py) and the native fixture
// (verification/probe/weapon_hull_reshade_cases.h) run the same match and model.
namespace x3m::weapon_effects::hull_reshade {
// The two program pairs an impact body is drawn with through the material submit (hull-hit-draw.md section 3), by the
// proxy's FNV-1a 64 of the bytes the game passes to CreateVertexShader. The VS is the ABI gate (what our program reads),
// never the identity: the identity is the scope node's body id.
//   effects / effects2s DEFAULT: VS d5e1c75351ed3f04 (vs_2_0: oT0.xy the UV after g_TexMatrix c10..c11, oD0.x
//     g_AlphaValue c12 x the optional fog clip), so our program is ps_2_0 (the lone bullet program's pairing);
//   standard_lighting(2s) DEFAULT: VS 494fe349b8bc12ec (vs_3_0: TEXCOORD0 the UV after g_TexMatrix c37..c38, COLOR0.a
//     g_AlphaValue c39.x x the optional fog factor), ps_3_0 (the muzzle and lone classic programs' pairing).
constexpr std::uint64_t effects_vertex_shader = 0xd5e1c75351ed3f04ull;
constexpr std::uint64_t classic_vertex_shader = 0x494fe349b8bc12ecull;
static_assert(classic_vertex_shader == reshade::classic_vertex_shader &&
                  classic_vertex_shader == muzzle_reshade::vertex_shader &&
                  effects_vertex_shader == reshade::lone_vertex_shaders[0],
              "the hull hit route gates on the VS the lone and muzzle routes gate on");
// g_AlphaValue per pair (the node fade; the frame row's cool_active= reads the application's value).
constexpr unsigned effects_alpha_register = 12, classic_alpha_register = 39;
enum Pair : std::uint8_t { pair_effects = 0, pair_classic = 1, pair_none = 2 };
// The admitted blend per body, reshade::LoneBlend's classes: ADD ONE / ONE (additive) or ADD ONE / INVSRCCOLOR (screen).
constexpr std::uint8_t blend_screen = reshade::lone_screen, blend_additive = reshade::lone_additive;
// One impact body: its id (node +0x144, FL), its texture's luminance normaliser (luminance_reference / the p99 of the
// texture's lit level-0 texels, luminance > 0.02, floored at 0.5 for a haze texture; flipbooks their first page:
// hull_hit_bodies.py says why the lit texels and the floor, not every texel as the design wrote), 4 for a 4 x 4 cell
// flipbook (the cell envelope) else 0,
// the pair it draws with, the blend admitted (v/10653's is unknown in the census: ONE / ONE only), and whether a
// non-impact row also draws it (the frame row's shared=; hull-hit-look.md section 2).
struct Body {
    std::uint32_t id;
    float normaliser;
    std::uint8_t cells, pair, blend;
    bool shared;
};
// The FL table, emitted by verification/results/fl-hull-hit-draw/hull_hit_bodies.py (hull_hit_bodies_out.txt; the host
// test holds these rows to it): every census impact body but the three death-shared ones (v/10656, 10664, 10670: the
// explosions family claims them) and bullet_ElectroSpike (the ESR's own bolt, the bullet route's). Most frequent first
// (the TBullets rows drawing the body), so a hit costs few compares. AP has no table yet (body_count(edition_ap) 0): its
// impact draws stay vanilla.
constexpr Body bodies[] = {
    {587u, 1.6965f, 0u, pair_classic, blend_additive, true}, // 380, lit p99 1.0000
    {10663u, 3.0376f, 4u, pair_classic, blend_additive, false}, // exp_sparks1_diff, lit p99 0.5585
    {469u, 1.6965f, 0u, pair_classic, blend_additive, false}, // 380, lit p99 1.0000
    {564u, 1.8751f, 0u, pair_classic, blend_additive, true}, // 383, lit p99 0.9047
    {12000u, 1.6965f, 4u, pair_effects, blend_additive, false}, // fx_blueexplosion_01, lit p99 1.0000
    {10658u, 1.7730f, 0u, pair_classic, blend_additive, false}, // exp_impact_glow_diff, lit p99 0.9569
    {10661u, 3.3929f, 0u, pair_classic, blend_additive, true}, // exp_glow_diff, lit p99 0.1326
    {582u, 1.6965f, 0u, pair_classic, blend_additive, true}, // 380, lit p99 1.0000
    {12001u, 1.6965f, 4u, pair_effects, blend_additive, true}, // fx_electric_01, lit p99 1.0000
    {589u, 1.7075f, 0u, pair_classic, blend_additive, true}, // 131, lit p99 0.9936
    {10388u, 1.6965f, 0u, pair_classic, blend_additive, true}, // 873, lit p99 1.0000
    {10667u, 3.3929f, 0u, pair_classic, blend_additive, true}, // fx_Lightning2_diff, lit p99 0.2420
    {10668u, 3.3929f, 0u, pair_classic, blend_additive, false}, // fx_wave_diff, lit p99 0.2006
    {12004u, 1.6965f, 4u, pair_effects, blend_additive, false}, // fx_MAMexplosion_01, lit p99 1.0000
    {12005u, 1.6965f, 4u, pair_effects, blend_additive, false}, // fx_smallGreenExplosion_01, lit p99 1.0000
    {12007u, 2.0542f, 0u, pair_effects, blend_screen, false}, // fx_sphereshockwave_diff, lit p99 0.8259
    {12008u, 1.6965f, 4u, pair_effects, blend_additive, false}, // fx_starburstExplosion_01, lit p99 1.0000
    {201u, 2.1384f, 0u, pair_classic, blend_additive, false}, // 376, lit p99 0.7933
    {10653u, 1.7154f, 4u, pair_classic, blend_additive, false}, // exp_small_diff, lit p99 0.9890
    {10659u, 3.3929f, 0u, pair_effects, blend_screen, false}, // exp_PL_imp_diff, lit p99 0.4913
    {10666u, 3.3929f, 0u, pair_classic, blend_additive, false}, // fx_Lightning_diff, lit p99 0.1562
    {12002u, 1.7970f, 4u, pair_effects, blend_additive, false}, // fx_blueimplosion_01, lit p99 0.9441
    {12003u, 1.7274f, 4u, pair_effects, blend_additive, false}, // fx_greenexplosion_01, lit p99 0.9821
    {12006u, 1.6965f, 4u, pair_effects, blend_additive, false}, // fx_surfacefire_01, lit p99 1.0000
};
constexpr unsigned table_count = sizeof bodies / sizeof bodies[0];
static_assert(table_count == 24 && table_count < 256, "MotionRoute::hull_body is a byte");
constexpr unsigned edition_ap = 0, edition_fl = 1;
constexpr unsigned build_edition = core::edition_fl ? edition_fl : edition_ap;
constexpr unsigned body_count(unsigned e = build_edition) noexcept { return e == edition_fl ? table_count : 0u; }
// The body's row, or table_count (not listed for the edition). One compare per listed body.
constexpr unsigned body_index(std::uint32_t id, unsigned e = build_edition) noexcept {
    const unsigned n = body_count(e);
    unsigned i = 0;
    while (i < n && bodies[i].id != id) ++i;
    return i < n ? i : table_count;
}
constexpr bool listed_anywhere(std::uint32_t id) noexcept { return body_index(id, edition_fl) < table_count; }
static_assert(!listed_anywhere(10656u) && !listed_anywhere(10664u) && !listed_anywhere(10670u),
              "the death-shared bodies stay vanilla until the explosions family");
constexpr bool muzzle_disjoint() noexcept {
    for (unsigned i = 0; i < muzzle_reshade::body_count; ++i)
        if (listed_anywhere(muzzle_reshade::bodies[i].id)) return false;
    return true;
}
static_assert(muzzle_disjoint(), "no muzzle body is a hull hit body: one claimant");
// The constant block: two registers, the same content for both programs, inside the blocks the shadows already keep:
// c30..c31 on ps_2_0 (the lone bullet block c29..c31, put back from ps_c29 + 1 row) and c192..c193 on ps_3_0 (the
// muzzle block c192..c195, put back from ps_c192). Each pair's draws bind only its own program, so the two never meet.
//   first  = (hh_gain, hh_edge, hh_core, the body's normaliser)
//   second = (hh_cool, cells (4: the cell envelope, 0: none), 0, 0)
constexpr unsigned effects_register = 30, classic_register = 192, constant_count = 2, constant_floats = 8;
static_assert(effects_register >= reshade::block_register &&
                  effects_register + constant_count <= reshade::block_register + reshade::block_count,
              "the effects block lies inside the shadowed c29..c31");
static_assert(classic_register >= muzzle_reshade::constant_register &&
                  classic_register + constant_count <= muzzle_reshade::constant_register + muzzle_reshade::constant_count,
              "the classic block lies inside the shadowed c192..c195");
constexpr unsigned effects_shadow_offset = (effects_register - reshade::block_register) * 4; // floats into ps_c29
constexpr unsigned classic_shadow_offset = (classic_register - muzzle_reshade::constant_register) * 4; // into ps_c192
inline void constants(const Body& b, const projectile_family::Tune& t, float out[constant_floats]) noexcept {
    namespace pf = projectile_family;
    out[0] = pf::clamp_key(t.hh_gain, pf::key_hh_gain);
    out[1] = pf::clamp_key(t.hh_edge, pf::key_hh_edge);
    out[2] = pf::clamp_key(t.hh_core, pf::key_hh_core);
    out[3] = b.normaliser;
    out[4] = pf::clamp_key(t.hh_cool, pf::key_hh_cool);
    out[5] = float(b.cells);
    out[6] = out[7] = 0.f;
}
// The match, the whole decision MotionOutput::hull_reshade_match takes for a draw no earlier route substituted (the
// fixture runs it as is), in its order: the VS (the pair; any other VS ends here), that pair's program ready, blend
// enabled and ADD ONE / ONE or ONE / INVSRCCOLOR (an opaque or alpha-blended draw ends before any read: not a
// candidate); then the scope node's body id (body_id(&id): the TLS scope lookup and one 4-byte validated read); a body
// the bullet body map holds (mapped(id): the lone classic and lone effects routes' claim, taken or refused there) and a
// listed muzzle body (the muzzle route's) are left to them; then the edition's list, the body's pair and admitted blend,
// and on the classic pair the bound pixel program one of the edition's standard_lighting DEFAULT pair (the material-type
// gate, the lone classic route's). body < table_count admits; candidate: the draw reached the node read.
struct Match {
    bool candidate = false;
    unsigned body = table_count;
    std::uint8_t pair = pair_none, blend = reshade::lone_none;
    std::uint32_t id = 0;
};
template <class BodyId, class Mapped>
inline Match match(std::uint64_t vs, bool blend_on, std::uint32_t src, std::uint32_t dst, std::uint32_t op,
                   std::uint64_t ps, bool ready_effects, bool ready_classic, BodyId body_id, Mapped mapped,
                   unsigned e = build_edition) noexcept {
    Match m;
    const std::uint8_t pair = vs == effects_vertex_shader ? pair_effects
                              : vs == classic_vertex_shader ? pair_classic
                                                            : pair_none;
    if (pair == pair_none || !(pair == pair_effects ? ready_effects : ready_classic) || !body_count(e)) return m;
    const unsigned blend = reshade::lone_blend(blend_on, src, dst, op);
    if (blend == reshade::lone_none) return m;
    m.candidate = true;
    if (!body_id(&m.id) || mapped(m.id)) return m;
    if (muzzle_reshade::body_index(m.id) < muzzle_reshade::body_count) return m;
    const unsigned b = body_index(m.id, e);
    if (b >= table_count || bodies[b].pair != pair || bodies[b].blend != blend) return m;
    if (pair == pair_classic && !reshade::classic_pixel_shader(ps, e == edition_fl ? reshade::pair_fl : reshade::pair_ap))
        return m;
    m.body = b;
    m.pair = pair;
    m.blend = std::uint8_t(blend);
    return m;
}
// One frame's counts (MotionOutput::hull_reshade_take_frame -> weapon_effects_patch.cpp's hull_reshade_frame row):
// candidates the blended draws of either pair that reached the node read, matched those admitted, effects / classic the
// draws substituted per pair, failures the substitutions refused or not restored (the draw left vanilla), zwrite_on the
// substituted draws drawn with Z write on, cool_active those whose application g_AlphaValue was below 1 (the fade and
// cooling at work; the fog factor is not in it), shared those of a body a non-impact row also draws, timed the draws
// whose sequence was QPC-timed and us its CPU time over them.
struct Frame {
    unsigned candidates = 0, matched = 0, effects = 0, classic = 0, failures = 0, zwrite_on = 0, cool_active = 0,
             shared = 0, timed = 0;
    float us = 0.f;
};
// The CPU model of the law (weapon_hull_reshade.hlsl hull_rgb; the fixture's reference for both programs): the colour of
// one sample. c the block (8 floats), t the diffuse texel (RGBA 0..1), u v the UV the VS passes (after g_TexMatrix), f
// the fade (COLOR0.x on the effects pair, COLOR0.a on the classic pair; saturated). Alpha: effects the texel's;
// classic lerp(t.a, light_a, glow) x COLOR0.a (vanilla's lane).
//   l     = saturate(luminance(t) x normaliser)
//   body  = gain x smoothstep(0, edge, l) x t.rgb x (1 - cool (1 - f) (0, 0.65, 0.9))    (cooling: white -> orange -> red)
//   core  = core x l^4 x lerp(white, t.rgb / max(t.rgb, 1/256), 0.35)
//   env   = 1 - saturate(cells) x smoothstep(0.85, 1, 2 max |frac(cells uv) - 1/2|)   (the flipbook cell's edge guard)
//   rgb   = (body + core) x f x env
inline float smooth01(float a, float b, float x) noexcept {
    float k = (x - a) / (b - a);
    k = k < 0.f ? 0.f : k > 1.f ? 1.f : k;
    return k * k * (3.f - 2.f * k);
}
inline float frac(float x) noexcept {
    const float i = x > -1e9f && x < 1e9f ? float(static_cast<int>(x)) : 0.f; // the model's UVs are small
    const float r = x - i;
    return r < 0.f ? r + 1.f : r;
}
inline void model(const float c[constant_floats], const float t[4], float u, float v, float f, bool classic,
                  float light_a, float glow, float out[4]) noexcept {
    const float fs = f < 0.f ? 0.f : f > 1.f ? 1.f : f;
    float l = (0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2]) * c[3];
    l = l < 0.f ? 0.f : l > 1.f ? 1.f : l;
    const float body = c[0] * smooth01(0.f, c[1], l);
    float peak = t[0] > t[1] ? t[0] : t[1];
    peak = peak > t[2] ? peak : t[2];
    peak = peak > 1.f / 256.f ? peak : 1.f / 256.f;
    const float l2 = l * l, core = c[2] * l2 * l2;
    const float qx = frac(u * c[5]) - 0.5f, qy = frac(v * c[5]) - 0.5f;
    const float ax = qx < 0.f ? -qx : qx, ay = qy < 0.f ? -qy : qy;
    const float on = c[5] < 0.f ? 0.f : c[5] > 1.f ? 1.f : c[5];
    const float env = 1.f - on * smooth01(0.85f, 1.f, 2.f * (ax > ay ? ax : ay));
    const float cool = c[4] * (1.f - fs), keep[3] = {0.f, 0.65f, 0.9f};
    for (unsigned i = 0; i < 3; ++i)
        out[i] = (body * t[i] * (1.f - cool * keep[i]) + core * (0.65f + 0.35f * t[i] / peak)) * (fs * env);
    out[3] = classic ? (t[3] + (light_a - t[3]) * glow) * f : t[3];
}
} // namespace x3m::weapon_effects::hull_reshade
