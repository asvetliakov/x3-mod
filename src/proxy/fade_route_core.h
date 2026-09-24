#pragma once
#include "sse_scalar.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

// Fade-band motion arm (docs/architecture/linear-distance-fade-region.md,
// "Fade-band route"; docs/reverse-engineering/asteroid-fog-temporal.md, run
// 49): pure admission helpers, no D3D. A reviewed motion pair drawn in the
// engine's exact fade-band state (Z test on, Z-write off, alpha test off,
// SRCALPHA/INVSRCALPHA ADD, RGB mask 7, sRGB write off, separate alpha off)
// is routed like an opaque draw when its fade fraction estimate reaches the
// threshold, instead of being composed by the fade bracket and masked
// current-only. With the RT2 owner on under original shading the same state
// with the alpha test on is admitted too (state's tested_ok). Host-tested through verification/probe/fade_region_host.cpp
// (--fade-route) from verification/analysis/test_fade_region.py.
namespace x3m::fade_route {
// The seven distance-fade vertex programs (linear_distance_fade.h) and the
// registers their CTAB names: g_AlphaValue (float4, .x used), g_FogClip
// (float4, .xy used), g_EnableFog = b0 for every one (shader sweep
// inventory, verification/results/shader-sweep-inventory.json; the loop
// programs carry c39/c41, the two fixed-light programs c18/c20). Rows 8 and 9
// (docs/architecture/fade-rt2-ownership.md section 3): the run214 station
// families' vertex programs 494fe349b8bc12ec and 53a0a641107ed76c (vs_3_0,
// runtime captured) declare the same three at c39 / c41 / b0. They have no
// distance_fade_rows pair, so the arm admits them only with X3M_FADE_RT2_OWNER
// on (arm_pair below); the glass c30104cb0efb6675 and damage 37c34a7478544c14
// programs, whose fade-band-state draws are material transparency, are not
// rows and keep the overlay path.
struct Registers { std::uint8_t alpha = 0, fog = 0; };
// The single source for this arm: registers() scans this table and the
// shader-population classifier (src/renderer/shader_population.h) enumerates
// the same rows, so neither form can admit a program the other does not.
struct VertexProgram { std::uint64_t hash; Registers registers; };
inline constexpr VertexProgram vertex_programs[] = {
    {0xb0602757fce6e870ull, {39, 41}}, {0x0c223ad11bce02d5ull, {39, 41}},
    {0x167eb2d5629ab9d3ull, {39, 41}}, {0x330ceb9dd874ede2ull, {39, 41}},
    {0x4944d81dfe531b37ull, {39, 41}},
    {0x233d17d26ce0c1fcull, {18, 20}}, {0x12b8a13f13fe8cfeull, {18, 20}},
    {0x494fe349b8bc12ecull, {39, 41}}, {0x53a0a641107ed76cull, {39, 41}}};
inline constexpr std::size_t vertex_program_count =
    sizeof vertex_programs / sizeof vertex_programs[0];
static_assert(vertex_program_count == 9, "seven distance-fade vertex programs and the two run214 station families");
constexpr bool registers(std::uint64_t vs, Registers& out) noexcept {
    for (const auto& row : vertex_programs)
        if (row.hash == vs) { out = row.registers; return true; }
    return false;
}
// The arm's pair identity (MotionOutput's shadow_.fade_route_pair; gate 3 has
// already required a reviewed motion pair): a registers row is required
// always; without the widening the pair must also be one of the bracket's
// distance_fade_rows (the seven pairs of the earlier builds), with it a
// registers row alone makes a fade pair (fade-rt2-ownership.md section 3).
// The route widens with X3M_FADE_RT2_OWNER under original shading only.
// The bracket's own identity (linear_distance_fade_sampler_mask) is not this
// function and admits nothing new.
constexpr bool arm_pair(bool registers_row, bool distance_fade_pair, bool widen) noexcept {
    return registers_row && (widen || distance_fade_pair);
}

// The exact fade-band render state (the fade route's nine-state check plus
// the separate-alpha switch off). Public D3D9 enum values: D3DZB_TRUE 1,
// D3DBLEND_SRCALPHA 5, D3DBLEND_INVSRCALPHA 6, D3DBLENDOP_ADD 1.
// tested_ok (docs/architecture/fade-alpha-cutout-ownership.md option A): the
// same state with the alpha test on (TRUE) is the fade-band stations'
// alpha-tested cutouts; the caller passes it only for a fade pair (not the
// overlay arm) with the RT2 owner on under original shading. The D3D9 alpha
// test discards a fragment before the depth write and every target write,
// so the routed variant (whose oC0.a is the original's) writes RT1/RT2
// exactly where the engine's colour draw lands.
constexpr bool state(std::uint32_t z, std::uint32_t z_write, std::uint32_t alpha_test, std::uint32_t blend,
                     std::uint32_t color_mask, std::uint32_t srgb_write, std::uint32_t src, std::uint32_t dst,
                     std::uint32_t op, std::uint32_t separate_alpha, bool tested_ok) noexcept {
    return z == 1 && z_write == 0 && (alpha_test == 0 || (tested_ok && alpha_test == 1)) && blend != 0 && color_mask == 7
        && srgb_write == 0 && src == 5 && dst == 6 && op == 1 && separate_alpha == 0;
}
// Distance of the object origin to the camera from the draw's clip rows
// (four rows as uploaded: clip = row_k . (x, y, z, 1), so the origin's clip
// is the translation column) and the camera's projection scales (row-vector
// D3D perspective with P[11] = 1: x_c = x_v m00 + z_v m20, y_c = y_v m11 +
// z_v m21, w_c = z_v; camera_reprojection.h). Without a valid camera the
// draw is refused (the bracket keeps it): the view depth w alone is a lower
// bound of the distance, so it overestimates the fraction and would route
// draws below the threshold. The sign of w does not matter: an origin behind
// the camera plane (w <= 0: a station module the camera has entered or
// passed, asteroid-fog-temporal.md "Run 130") is at the same Euclidean
// distance the inversion yields for w > 0, and the mesh in front of the
// camera is admitted by its origin exactly as any straddling mesh is. False
// for a nonfinite input only.
inline bool origin_distance(const float rows[16], bool camera_valid, float m00, float m11, float m20, float m21,
                            float& out) noexcept {
    const float xc = rows[3], yc = rows[7], w = rows[15];
    if (!std::isfinite(xc) || !std::isfinite(yc) || !std::isfinite(w)) return false;
    if (!camera_valid || !(m00 > 0.f) || !(m11 > 0.f) || !std::isfinite(m20) || !std::isfinite(m21)) return false;
    const float xv = (xc - w * m20) / m00, yv = (yc - w * m21) / m11;
    const float d = scalar::sqrt(xv * xv + yv * yv + w * w);
    if (!std::isfinite(d)) return false;
    out = d; return true;
}
// The shadow-caster candidate's origin distance (MotionOutput::
// note_candidate_distance, shadow_replay admission): the pre-run-130 contract.
// An origin at or behind the camera plane (w <= 0, or a NaN w) has no origin
// distance, so the candidate has no origin rule and only its extent decides
// (motion_output.cpp, note_candidate_draw). Only the fade arm takes the
// behind-camera distance above.
inline bool origin_distance_front(const float rows[16], bool camera_valid, float m00, float m11, float m20, float m21,
                                  float& out) noexcept {
    return rows[15] > 0.f && origin_distance(rows, camera_valid, m00, m11, m20, m21, out);
}
// Light-map far fade (--light-map-far-fade P0,P1[,G]; taa-distant-line-fade.md
// section 11): the effective hull light-map gain of one draw. The footprint is
// the far stabiliser's measure, world units per pixel `2 z / (p00 * width)`,
// evaluated at the object origin's view depth z = rows[15] (clip w). The gain
// is `gain` up to P0, `floor` from P1, linear in the footprint between them
// (inv = 1 / (P1 - P0)); both ends are returned exactly, so a near draw is bit
// identical to the constant gain. No camera, a nonpositive scale or an origin
// at or behind the camera plane (a large object around the viewer: near)
// keeps the configured gain. One division, no branches on the device.
inline float lightmap_far_gain(float w, bool camera_valid, float m00, float width, float gain, float floor,
                               float p0, float inv) noexcept {
    if (!camera_valid || !(m00 > 0.f) || !(width > 0.f) || !(w > 0.f)) return gain;
    const float t = (2.f * w / (m00 * width) - p0) * inv;
    if (!(t > 0.f)) return gain;
    if (t >= 1.f) return floor;
    return gain + (floor - gain) * t;
}
// Hull emissive widening (--hull-emissive-widening K[,B];
// hull-emissive-widening.md section 8.3 R1): the per-draw lane the widened
// block multiplies the squared UV gradient by, (size . K)^2 for one axis of
// the bound light map (level-0 texels), so that the block's k = clamp(K .
// texels per pixel, 1, K) is the light map's own footprint, not the object
// origin's distance. An unknown size (0: no 2D texture on the stage yet)
// gives 0, which the block reads as k = 1. Two multiplies; SSE scalar only.
inline float lightmap_widen_scale(std::uint32_t size, float k) noexcept {
    if (!size || !(k > 0.f)) return 0.f;
    const float scaled = float(size) * k;
    return scaled * scaled;
}
// The vertex program's alpha (asteroid-fog-temporal.md, "Exact shader alpha"):
// COLOR0.a = g_AlphaValue.x * saturate(g_FogClip.x - g_FogClip.y * distance)
// with fog, g_AlphaValue.x without. Evaluated at the origin distance, so a
// large mesh straddling the band is admitted by its origin. Nonfinite
// inputs yield 0 (never admitted).
inline float fraction(float alpha_x, bool fog, float fog_x, float fog_y, float distance) noexcept {
    if (!std::isfinite(alpha_x) || !std::isfinite(fog_x) || !std::isfinite(fog_y) || !std::isfinite(distance)) return 0.f;
    float factor = 1.f;
    if (fog) {
        factor = fog_x - fog_y * distance;
        if (!(factor > 0.f)) factor = 0.f;
        if (factor > 1.f) factor = 1.f;
    }
    const float f = alpha_x * factor;
    return std::isfinite(f) ? f : 0.f;
}
// Per mille of the fraction, clamped to [0, 1000].
inline unsigned permille(float f) noexcept {
    if (!(f > 0.f)) return 0u;
    if (f >= 1.f) return 1000u;
    return unsigned(int(f * 1000.f)); // f in (0, 1): the signed conversion is one cvttss2si, the unsigned one x87 fistp
}
// Admission by threshold (per mille, X3M_FADE_ROUTE): the arm is off for a
// threshold above 1000; 0 admits every recognised fade-band draw.
constexpr unsigned threshold_off = 1001u;
constexpr bool admit(unsigned f_permille, unsigned threshold_permille) noexcept {
    return threshold_permille <= 1000u && f_permille >= threshold_permille;
}
// Hysteresis at the threshold, keyed by the draw's node identity: an object
// whose estimate hovers about X3M_FADE_ROUTE would otherwise alternate
// between the route and the bracket every frame (the composite steps between
// the native encoded-space mix and the bracket's linear source-over). A key
// admitted at >= threshold stays admitted while its estimate is >= threshold
// - band; a key refused, not seen for more than `expiry` frames, or evicted
// from the table (the oldest entry goes) starts again at the threshold. Key
// 0 (no identity) is decided by the threshold alone and never stored.
// Fixed storage, a linear scan per recognised fade-band draw. `evicted`
// counts the entries a full table displaced (the oldest one each time) since
// the owner last read it (the fade_route_frame line's fade_evicted).
struct Hysteresis {
    static constexpr unsigned band = 100u, capacity = 64u;
    static constexpr std::uint64_t expiry = 8u;
    struct Entry { std::uint64_t key = 0, frame = 0; bool armed = false; };
    Entry entries[capacity]{};
    unsigned count = 0;
    std::uint32_t evicted = 0;
    void clear() noexcept { count = 0; }
    // `held`: admitted below the threshold by the band only.
    bool admit(std::uint64_t key, std::uint64_t frame, unsigned f_permille, unsigned threshold_permille, bool& held) noexcept {
        held = false;
        if (threshold_permille > 1000u) return false;
        if (!key) return f_permille >= threshold_permille;
        Entry* entry = nullptr; unsigned oldest = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (entries[i].key == key) { entry = &entries[i]; break; }
            if (entries[i].frame < entries[oldest].frame) oldest = i;
        }
        const bool armed = entry && entry->armed && frame >= entry->frame && frame - entry->frame <= expiry;
        const unsigned low = threshold_permille > band ? threshold_permille - band : 0u;
        const bool admitted = f_permille >= threshold_permille || (armed && f_permille >= low);
        held = admitted && f_permille < threshold_permille;
        if (!entry) {
            if (count < capacity) entry = &entries[count++];
            else { entry = &entries[oldest]; ++evicted; }
            entry->key = key;
        }
        entry->frame = frame; entry->armed = admitted;
        return admitted;
    }
};
} // namespace x3m::fade_route
