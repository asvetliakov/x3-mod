#pragma once
#include "muzzle_reshade_core.h" // the muzzle bodies the lone classic match leaves to the muzzle re-shade
#include "weapon_projectile_family.h"
#include <cmath>
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
// The lone path (bullet-draw-batch.md section 7): a bullet body the batch does not take (fewer than two bolts of the
// body in the layer, over the batch budget or capacity) is drawn through the material submit (0x0e7142 -> 0x0e0f30)
// with its own effects.fx material, inside the FL object-trace scope. Its vertex shader (the proxy's FNV-1a 64 of the
// bytes the game passes to CreateVertexShader) is the effects/effects2s DEFAULT one of every SM2+ profile directory,
// or the INSTANCE one (the two share the interface our lone program reads: oT0.xy the UV, oD0.x the fade =
// g_AlphaValue x the optional fog clip, oD0.yz 0; verification/results/projectile-reshade/lone_inputs_out.txt). The
// VS is shared with every other effects-material draw (engine glows, gate, flares), so it is the ABI gate, not the
// identity: the identity is the scope node's body id (node +0x144) in the body map below.
constexpr std::uint64_t lone_vertex_shaders[2] = {0xd5e1c75351ed3f04ull, 0x89193868c61c3846ull};
constexpr bool lone_vertex_shader(std::uint64_t hash) noexcept {
    return hash && (hash == lone_vertex_shaders[0] || hash == lone_vertex_shaders[1]);
}
// The lone classic path (bullet-draw-batch.md section 7 "Classic bodies", beam-bullet-draw.md section 5): a classic
// (MATERIAL3/5/6) bullet body the batch does not take is drawn through the same material submit with the
// standard_lighting DEFAULT technique, the one-sided or the two-sided (MPF_2SIDED) instance. Both bind the vertex shader
// 494fe349b8bc12ec (vs_3_0: TEXCOORD0.xy the UV after g_TexMatrix c37..c38, COLOR0.a = g_AlphaValue c39.x x the
// optional fog factor, the node fade; no vertex colour in its input) and one of the two pixel programs (FNV-1a 64 of the bytes the
// game passes to CreatePixelShader; FL 3_0 archive, verification/results/fl-beam-bullet-draw/beam_program_inputs_out.txt).
// The pair is shared by every opaque classic body and by the muzzle flash body 445 (muzzle_reshade_core.h): the VS is the
// ABI gate, the PS the material-type gate (the effects bodies never bind it), the identity is the scope node's body id in
// the body map below, which every TBullets body is in (tables.bullet_bodies holds a row per body string the rows draw,
// classic `objects/v/NNNNN` bodies included). No muzzle flash body (muzzle_reshade_core.h's seven) is in a TBullets row
// (verification/results/weapon-coverage-census/); MotionOutput runs this route first, leaves a listed muzzle body to the
// muzzle route even if a row named it, and the muzzle route skips a draw this one re-shaded: one draw, one claimant.
// The pixel programs differ by edition (docs/reverse-engineering/ap-bullet-batch-site.md section 2: the VS is the same
// bytes in AP stock, AP Mayhem and FL; AP's standard_lighting / 2s DEFAULT PS are 1298 / 1324 dwords, FL's 1418 / 1450,
// neither pair present in the other edition's catalogues), so the table holds both pairs and each edition's decision
// reads its own row (`build_pair`, the build's edition; the native fixture passes either explicitly).
constexpr std::uint64_t classic_vertex_shader = 0x494fe349b8bc12ecull;
constexpr unsigned pair_ap = 0, pair_fl = 1;
constexpr unsigned build_pair = core::edition_fl ? pair_fl : pair_ap;
constexpr std::uint64_t classic_pixel_shader_pairs[2][2] = {
    {0x7c83ed50c9894e44ull /* AP standard_lighting */, 0xe70adc744a38ca59ull /* AP standard_lighting2s */},
    {0xced7edef48059e84ull /* FL standard_lighting */, 0x0d5848c8489b6630ull /* FL standard_lighting2s */}};
inline constexpr const std::uint64_t (&classic_pixel_shaders)[2] = classic_pixel_shader_pairs[build_pair]; // the build's
constexpr bool classic_pixel_shader(std::uint64_t hash, unsigned e = build_pair) noexcept {
    return hash && e < 2 && (hash == classic_pixel_shader_pairs[e][0] || hash == classic_pixel_shader_pairs[e][1]);
}
// Why a blended lone classic draw of a mapped bullet body stayed vanilla (bullet_reshade_frame
// lone_classic_refused_why=): blend: not ADD ONE / ONE (an MPF_ALPHABLEND material, SRCALPHA / INVSRCALPHA, e.g. the
// EMP's v/00517; an MPF_NULL part the node fade blends SRCALPHA / ONE, e.g. the Mass Driver's textures 282 and 36; ONE /
// INVSRCCOLOR, which no classic bullet material has, so the additive route's conversion is never offered a classic draw);
// program: the bound pixel shader is not a standard_lighting DEFAULT one; unready: our classic program was not created
// or the re-shade stopped; failed: the substitution failed (all undone, the draw vanilla).
enum ClassicRefused : unsigned { classic_blend = 0, classic_program, classic_unready, classic_failed, classic_refused_count };
constexpr const char* classic_refused_names[classic_refused_count] = {"blend", "program", "unready", "failed"};
// The verdict on a blended lone classic draw of a mapped body (classic_match below): blend the lone_blend class (only
// lone_additive, ONE / ONE, is admitted), ps the bound pixel program's hash, ready our classic program created and the
// re-shade not stopped; e the edition whose pixel pair admits. classic_refused_count admits; anything else is the
// refusal's reason.
constexpr unsigned classic_verdict(unsigned blend, std::uint64_t ps, bool ready, unsigned e = build_pair) noexcept {
    return blend != 2u /* lone_additive */ ? classic_blend
           : !classic_pixel_shader(ps, e) ? classic_program
           : !ready                       ? classic_unready
                                          : classic_refused_count;
}
// The node's body (model) id, FL node +0x144 (bullet-draw-batch.md section 4), AP +0x140 (ap-bullet-batch-site.md
// section 3: the body bind 0x00487e30 stores it, the batch lookup and the material-submit scope read it), not a bullet
// mark: a non-bullet node drawing a body a TBullets row also names, with the effects VS and blend, would match too (no
// census of bodies shared outside TBullets was made).
constexpr std::uint32_t node_body_offset = core::layout::node_body;
static_assert(node_body_offset == (core::edition_fl ? 0x144u : 0x140u), "the edition's node body id");
// The lone draw's blend (the material's g_SrcBlend / g_DestBlend; every effects-material bullet body of the FL table is
// ONE / INVSRCCOLOR but bullet_push and tol_bullet, ONE / ONE): lone_screen is the batch pass's own law (the additive
// route converts it), lone_additive draws plain under the game's ONE / ONE, anything else is not a bullet draw.
enum LoneBlend : unsigned { lone_none = 0, lone_screen = 1, lone_additive = 2 };
constexpr unsigned lone_blend(bool blend, std::uint32_t src, std::uint32_t dst, std::uint32_t op) noexcept {
    return !blend || src != 2u /* D3DBLEND_ONE */ || op != 1u /* D3DBLENDOP_ADD */ ? lone_none
           : dst == 4u /* D3DBLEND_INVSRCCOLOR */                                  ? lone_screen
           : dst == 2u /* D3DBLEND_ONE */                                          ? lone_additive
                                                                                    : lone_none;
}
// The lone classic match, the whole decision MotionOutput::lone_bullet_match takes for a draw while the
// standard_lighting DEFAULT VS is bound (and the native fixture runs as is), in its order: the VS hash; blend enabled (an
// opaque draw ends here, before any read); the scope node's body id (body_id(&id): the TLS scope lookup and one 4-byte
// validated read; false ends the match); the body map (mapped(id): an unmapped body is no bullet, not counted); a listed
// muzzle body is the muzzle re-shade's (muzzle_reshade_core.h), not counted; then classic_verdict. Result: blend the
// admitted LoneBlend (lone_none unless admitted), id the body read, why classic_refused_count unless a mapped body was
// refused (the reason; MotionOutput counts it), raw the draw's lone_blend class for the refusal row.
struct ClassicMatch {
    unsigned blend = lone_none, raw = lone_none, why = classic_refused_count;
    std::uint32_t id = 0;
};
template <class BodyId, class Mapped>
inline ClassicMatch classic_match(std::uint64_t vs, bool blend_on, std::uint32_t src, std::uint32_t dst, std::uint32_t op,
                                  std::uint64_t ps, bool ready, BodyId body_id, Mapped mapped,
                                  unsigned e = build_pair) noexcept {
    ClassicMatch m;
    if (vs != classic_vertex_shader || !blend_on) return m;
    m.raw = lone_blend(blend_on, src, dst, op);
    if (!body_id(&m.id) || !mapped(m.id)) return m;
    if (muzzle_reshade::body_index(m.id) < muzzle_reshade::body_count) return m;
    m.why = classic_verdict(m.raw, ps, ready, e);
    if (m.why == classic_refused_count) m.blend = m.raw;
    return m;
}
// The program's constant register (weapon_bullet_reshade_ps.hlsl `reshade : register(c31)`): ps_2_0 has c0..c31; the
// game's bullet program reads none (section 6: `tex t0; mul r0.rgb, t0, v0.a; mov r0.a, t0.a`, no c#), and the block is
// put back after each substituted draw. The block is c29..c31: c29..c30 the drawn body's UV layout (layout_constants,
// the geometric size), c31 the look; one SetPixelShaderConstantF of three registers each way.
constexpr unsigned constant_register = 31;
constexpr unsigned block_register = 29, block_count = 3, layout_floats = 8;
static_assert(block_register + block_count - 1 == constant_register, "c31 closes the block");
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
// c31 from the tune (rs_gain, rs_edge, rs_core; clamped by the tune's bounds), w the IRE's normaliser (the device
// sequence uploads the draw's texture's instead, Plan::scale).
inline void constants(const projectile_family::Tune& t, float out[4]) noexcept {
    namespace pf = projectile_family;
    out[0] = pf::clamp_key(t.rs_gain, pf::key_rs_gain);
    out[1] = pf::clamp_key(t.rs_edge, pf::key_rs_edge);
    out[2] = pf::clamp_key(t.rs_core, pf::key_rs_core);
    out[3] = luminance_scale;
}
// The geometric size (docs/architecture/projectile-look.md "Geometric size (2026-11-10)"): the drawn body's UV layout
// (weapon_effects.json tables.bullet_bodies, tools/effects/weapon_effects_table.py uv_layout; reduced at load to
// 16.16 words, weapon_effects_table.h) and the keys rs_head / rs_width give c29..c30:
//   c29 = (head centre u, v, 1 / R, 1 / rs_head)       inside the head's square footprint (max |du|, |dv| <= 1.02 R):
//                                                        f = smoothstep(1, 1 - s, |uv - centre| / (R rs_head))
//   c30 = (fin axis v, 1 / (rim v - axis v), 1 / rs_width, s)   inside the fin band (d = (v - axis) / (rim - axis) in
//                                                        [-0.25, 1.02], any u): f = smoothstep(1, 1 - s, |d| / rs_width)
// and f = 1 elsewhere; the radiance is multiplied by f. A key at 1 uploads 0 for its 1 / rs: the falloff's argument is
// 0, f exactly 1 (the accepted look bit for bit). A body without a group gets a footprint no UV reaches (centre -100).
// The generator records a group only when no other triangle of the body samples inside its footprint, so the shader
// needs no other test.
constexpr float geometric_softness = 0.5f; // s: the outer half of the shrunk radius fades
constexpr float head_footprint = 1.02f, fin_low = -0.25f, fin_high = 1.02f; // the program's literals
// The table's layout words (16.16; core::Table::bullet_body_layout): flags (bit 0 head, bit 1 fin), head u, v, R, fin
// axis v, rim v, u0, u1 (u0 / u1 informational).
enum LayoutWord : unsigned { lw_flags = 0, lw_head_u, lw_head_v, lw_head_r, lw_axis_v, lw_rim_v, lw_u0, lw_u1 };
constexpr std::int32_t layout_head = 1, layout_fin = 2;
constexpr float identity_layout[layout_floats] = {-100.f, -100.f, 1.f, 0.f, -100.f, 1.f, 0.f, geometric_softness};
inline void layout_constants(const std::int32_t* w, const projectile_family::Tune& t, float out[layout_floats]) noexcept {
    namespace pf = projectile_family;
    const float head = pf::clamp_key(t.rs_head, pf::key_rs_head), width = pf::clamp_key(t.rs_width, pf::key_rs_width);
    for (unsigned i = 0; i < layout_floats; ++i) out[i] = identity_layout[i];
    if (!w) return;
    constexpr float q = 1.f / 65536.f;
    if ((w[lw_flags] & layout_head) && w[lw_head_r] > 0) {
        out[0] = float(w[lw_head_u]) * q;
        out[1] = float(w[lw_head_v]) * q;
        out[2] = 65536.f / float(w[lw_head_r]);
        out[3] = head < 1.f ? 1.f / head : 0.f;
    }
    if ((w[lw_flags] & layout_fin) && w[lw_rim_v] != w[lw_axis_v]) {
        out[4] = float(w[lw_axis_v]) * q;
        out[5] = 65536.f / float(w[lw_rim_v] - w[lw_axis_v]);
        out[6] = width < 1.f ? 1.f / width : 0.f;
    }
}
// The engine body id -> tables.bullet_bodies entry map (weapon_effects_patch.cpp bullet_body_layout; the host test
// verification/analysis/test_weapon_effects_table.py drives it with a fake reader). Built once: each table row whose
// body string hash names an entry gives the live TBullets row's word +0 (its body id, = the batch's [EDX+0x0c]).
// build_body_map returns false while the map is not final: the engine's TBullets not readable yet (no count), its live
// row count not the table's yet (a table still loading), or a row read failed; the last two are counted together, and
// after body_map_retries such builds the map is final without layouts (reason stale_rows or row_read_failed, the
// caller's row naming both counts). A final map is state 1 (built) or 2 (off: no table, no entries, or the retries
// spent). The caller attempts at most one build per frame (weapon_effects_patch.cpp bullet_body_layout), so the
// retries span 64 frames however many draws ask. read(address, out, bytes) -> bool is the validated engine reader.
constexpr unsigned tbullets_body = 0x0;  // TBullets +0x0: the row's body id (word; = node +0x144, AP +0x140)
constexpr std::uint32_t body_map_retries = 64;
struct BodyMap {
    std::uint8_t state = 0;    // 0 not final, 1 built, 2 off
    unsigned count = 0;
    std::uint32_t failures = 0; // builds refused for a row count mismatch or a failed row read
    std::uint32_t rows = 0;     // the engine's row count at the last build
    const char* reason = "unbuilt";
    std::uint32_t id[core::bullet_body_capacity]{};
    std::uint8_t entry[core::bullet_body_capacity]{};
};
template <class Read>
bool build_body_map(BodyMap& m, const core::Table* t, std::uintptr_t table_slot, std::uintptr_t count_slot,
                    Read read) noexcept {
    if (m.state) return true;
    const unsigned entries = t ? t->bullet_bodies : 0u;
    const auto off = [&](const char* why) {
        m.state = 2;
        m.count = 0;
        m.reason = why;
        return true;
    };
    if (!t) return off("no_table");
    if (!entries) return off("no_bullet_bodies");
    std::uint32_t table = 0, rows = 0;
    if (!table_slot || !count_slot || !read(table_slot, &table, 4) || !table || !read(count_slot, &rows, 4) || !rows)
        return false; // TBullets not loaded yet
    m.rows = rows;
    if (rows != t->rows) { // not the table's TBullets (yet): unbuilt, retried at the next attempt
        if (++m.failures >= body_map_retries) return off("stale_rows");
        return false;
    }
    m.count = 0;
    for (unsigned r = 0; r < rows && r < core::table_capacity && m.count < core::bullet_body_capacity; ++r) {
        if (!(t->bits[r] & core::row_present) || !t->body_hash[r]) continue;
        unsigned e = 0;
        while (e < entries && t->bullet_body_hash[e] != t->body_hash[r]) ++e;
        if (e == entries) continue;
        std::uint16_t id = 0;
        if (!read(std::uintptr_t(table) + std::uintptr_t(r) * core::layout::type_stride + tbullets_body, &id, 2)) {
            m.count = 0;
            if (++m.failures >= body_map_retries) return off("row_read_failed");
            return false; // asked again at the next draw
        }
        bool seen = false;
        for (unsigned i = 0; i < m.count && !seen; ++i) seen = m.id[i] == id;
        if (seen) continue;
        m.id[m.count] = id;
        m.entry[m.count++] = std::uint8_t(e);
    }
    m.state = 1;
    m.reason = "ok";
    return true;
}
// The layout words of a body id in a built map (nullptr: not built, no table, or no entry for the id).
inline const std::int32_t* body_layout(const BodyMap& m, const core::Table* t, std::uint32_t body) noexcept {
    if (m.state != 1 || !t) return nullptr;
    for (unsigned i = 0; i < m.count; ++i)
        if (m.id[i] == body) return m.entry[i] < t->bullet_bodies ? t->bullet_body_layout[m.entry[i]] : nullptr;
    return nullptr;
}
// The program's factor at uv for layout constants c (c29..c30), the CPU twin of weapon_bullet_reshade.hlsl geometric.
inline float geometric(const float c[layout_floats], float u, float v) noexcept {
    const auto fade = [](float x, float s) {
        float k = (1.f - x) / s;
        k = k < 0.f ? 0.f : k > 1.f ? 1.f : k;
        return k * k * (3.f - 2.f * k);
    };
    const float du = u - c[0], dv = v - c[1];
    const float box = (std::fabs(du) > std::fabs(dv) ? std::fabs(du) : std::fabs(dv)) * c[2];
    if (box <= head_footprint) return fade(std::sqrt(du * du + dv * dv) * c[2] * c[3], c[7]);
    const float d = (v - c[4]) * c[5];
    if (d >= fin_low && d <= fin_high) return fade(std::fabs(d) * c[6], c[7]);
    return 1.f;
}
// The CPU model of the program (the host test's reference; the fixture's readback checks it): radiance of one sample
// t (RGB 0..1) at vertex alpha a (the geometric factor, geometric(), multiplies it).
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
