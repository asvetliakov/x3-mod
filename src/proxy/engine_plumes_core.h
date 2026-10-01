#pragma once
#include <cstddef>
#include <cstdint>
#include "sse_scalar.h"
#include "engine_effects_core.h"
#include "../renderer/fog_transmittance.h"

// Portable core of the engine plumes, phase 2 (docs/architecture/engine-effects-modern.md sections 3-6): the strength
// presets and their parser, the Ctrl+Alt+F6 press latch, the tint of a record, and the CPU builder that turns the
// frame's glow-jet records (engine_effects_core.h Record) into the vertices of the stage's one indexed draw. No Windows
// dependency: the host tests compile it. No x87: float work through SSE scalars, no float returned by value from a
// function that may stay out of line.
//
// Per nozzle, two quads (8 vertices, 12 indices) in the camera's view space, projected by the vertex program:
// - the axial billboard: it contains the plume axis (record.axis = -(model z), the side the glow mesh extends to) and
//   is turned about that axis to face the camera; local (u along the axis from the nozzle, w across), length
//   L = z * value (the engine's own law, z the raw node+0x88 scale), core radius 0.15 value at the nozzle tapering to
//   0 at L, halo sigma 0.5 value (x the preset) tapering to half at the tip; the quad is a trapezoid that reaches
//   2.25 sigma (the local sigma) past every edge of the core and of the segment nozzle..tip, where the pixel program's
//   halo window reaches 0;
// - the nozzle disc: camera-facing (the view plane), hot core of diameter 0.5 value, the same halo, weighted by
//   |axis . to_camera| (it carries the look where the axial quad degenerates, head-on and tail-on); drawn only from a
//   weight of 0.15, its radiance fading in over 0.15..0.3 (a side view draws no disc).
// value = record.size (|model x| of the c4-6 rows: the body's LOD-0 value x the context scale). RCS jets (v/00566,
// flag_steering) take the same quad with L = z * value (short by construction: z runs 0.01..1.0 on steering) and
// their radiance x min(z, 1); below z 0.02 they are not drawn.
// Screen rules: a nozzle whose value projects under 1.5 px is not drawn; the core is at least 1.5 px in radius and a
// main jet's L at least 6 px (the 3x3-clip survival rule of the motes); the plume's projected width (2 sigma at the
// axis point nearest the camera) is clamped to 0.12 H by shrinking it about the nozzle, and its radiance fades
// 1 -> 0.5 over the last 20 % before the clamp (the own ship's plume in chase view; any plume that close).
// Occlusion depth (the pixel program): the nearest axis point's view depth, the nozzle's view z (intensity[3]) plus
// the axis's view z component (intensity[2]) x local u clamped to [0, L] (exact anywhere on the billboard, whose side
// vector has a view z component off-centre), pulled towards the camera by 0.5 value x max(0, axis . to_camera).
// View filter: only the records of the scene view are drawn (ViewFilter: recorded in the scene phase with the scene
// view's camera handle: the one the own ship's jets were recorded under, else the frame's most frequent handle among
// the scene-phase records); the rest are counted
// skipped_other_view (a target-monitor view would otherwise be projected with the scene camera).
// Fog (phase 3): with View::fog on, both colours are multiplied per channel by the stored-density look's mean
// transmittance at the nozzle's distance (renderer/fog_transmittance.h), the same factor the nozzle's ribbon takes.
namespace x3m::engine_plumes {
namespace ee = x3m::engine_effects::core;

// --------------------------------------------------------------------------- presets
// X3M_ENGINE_EFFECTS_PRESET=restrained|default|strong (ini engine_effects_preset), default "default"; Ctrl+Alt+F6
// cycles them at run time. Each scales I_core, I_halo and the halo sigma.
enum class Preset : std::uint8_t { restrained = 0, standard = 1, strong = 2 };
constexpr unsigned preset_count = 3;
constexpr Preset default_preset = Preset::standard;
inline const char* preset_name(Preset p) noexcept {
    return p == Preset::restrained ? "restrained" : p == Preset::strong ? "strong" : "default";
}
inline void preset_scale(Preset p, float* out) noexcept {
    *out = p == Preset::restrained ? .6f : p == Preset::strong ? 1.5f : 1.f;
}
inline Preset next_preset(Preset p) noexcept {
    return Preset((unsigned(p) + 1u) % preset_count);
}
// Exactly one of the three words in lower case (the engine_effects option's rule: mixed case, padding or another word
// is refused); `n` characters of narrow or wide text.
template <class Char> inline bool parse_preset(const Char* text, std::size_t n, Preset* out) noexcept {
    static const char* const words[preset_count] = {"restrained", "default", "strong"};
    if (!text) return false;
    for (unsigned w = 0; w < preset_count; ++w) {
        std::size_t len = 0;
        while (words[w][len]) ++len;
        if (n != len) continue;
        bool same = true;
        for (std::size_t i = 0; i < n && same; ++i) same = text[i] == Char(words[w][i]);
        if (same) {
            *out = Preset(w);
            return true;
        }
    }
    return false;
}
template <class Char> inline bool parse_preset(const Char* text, Preset* out) noexcept {
    if (!text) return false;
    std::size_t n = 0;
    while (text[n] && n < 16) ++n;
    if (text[n]) return false;
    return parse_preset(text, n, out);
}

// --------------------------------------------------------------------------- hotkey
// Ctrl+Alt+F6 with Shift up, edge-triggered on F6's own latch: a held F6 never becomes a press by changing modifiers,
// and an unfocused window neither fires nor arms (the latch follows the key while unfocused, so focus coming back with
// F6 held is no press).
struct PresetKey {
    bool f6_down = false;
    bool step(bool focused, bool control, bool alt, bool shift, bool f6) noexcept {
        const bool press = focused && control && alt && !shift && f6 && !f6_down;
        f6_down = f6;
        return press;
    }
};

// --------------------------------------------------------------------------- look
constexpr float core_radius = .15f;         // x value, at the nozzle
constexpr float halo_sigma = .5f;           // x value, at the nozzle (x the preset)
constexpr float disc_radius = .25f;         // x value: the hot disc's diameter is 0.5 value
constexpr float core_low = 1.5f, core_high = 4.f; // I_core(s) = lerp(1.5, 4.0, s)
constexpr float halo_low = .3f, halo_high = .8f;  // I_halo(s) = lerp(0.3, 0.8, s)
constexpr float flicker_amplitude = .1f;    // +-10 % on the core
constexpr unsigned flicker_frames = 8;      // one noise cell per 8 frames (7.5 Hz at 60 fps)
constexpr float soft_core = .15f, soft_halo = 1.f; // SOFT x value (the pixel program's lane terms)
constexpr float halo_reach = 2.25f;         // the quads reach 2.25 local sigma past the core (the halo window's zero)
constexpr float disc_min_weight = .15f;     // |axis . to_camera| under which no disc is drawn
constexpr float disc_fade_band = .15f;      // its radiance fades in over 0.15..0.3
constexpr float occlusion_bias = .5f;       // x value x max(0, axis . to_camera): the exhaust facing the camera clears its hull
constexpr float chase_cap = .12f;           // x H: the largest projected plume
constexpr float chase_fade_band = .2f;      // the last 20 % before the cap
constexpr float chase_fade_floor = .5f;     // the radiance at and past the cap
constexpr float min_core_px = 1.5f, min_length_px = 6.f, cull_px = 1.5f;
constexpr float steering_min_z = .02f;
constexpr unsigned max_nozzles = ee::ring_capacity; // 1,024: one ring
constexpr unsigned vertices_per_nozzle = 8, indices_per_nozzle = 12;
constexpr unsigned max_vertices = max_nozzles * vertices_per_nozzle; // 8,192: 16-bit indices
static_assert(max_vertices <= 65536u, "16-bit indices");

// Normalised linear tints of the clusters (tools/effects/engine_bodies.py CLUSTERS through the sRGB EOTF, divided by
// the largest channel), for a record without a table colour; grey and white are neutral.
inline const float* cluster_tint(unsigned cluster) noexcept {
    static const float tints[ee::cluster_count][3] = {
        {.1049f, .1049f, 1.f}, {.0287f, .6456f, 1.f}, {1.f, .0694f, .0694f}, {.1659f, 1.f, .4289f}, {1.f, 1.f, .0033f},
        {1.f, .1967f, .0441f}, {1.f, .1553f, .9596f}, {.6941f, .2667f, 1.f}, {1.f, 1.f, 1.f},       {1.f, .7911f, .6973f},
        {.0265f, 1.f, .0265f}, {.1350f, .6747f, 1.f},  {1.f, 1.f, 1.f}};
    return tints[cluster < ee::cluster_count ? cluster : ee::default_cluster];
}

// One vertex of the stage's VB: 68 bytes (FLOAT3, 3 x FLOAT4, 2 x D3DCOLOR).
struct Vertex {
    float position[3];  // view space (x right, y up, z forward), before the jittered projection
    float local[4];     // u | x, w | y (world units), L, core radius r0 at the nozzle
    float shape[4];     // halo sigma at the nozzle, value (the SOFT base), occlusion bias (view units), kind (0 axial, 1 disc)
    float intensity[4]; // I_core (x flicker x weights), I_halo (x weights), the axis's view z component (axial), the
                        // nozzle's view z
    std::uint32_t tint; // 0xAARRGGBB of the mean colour, largest channel 255
    std::uint32_t peak; // 0xAARRGGBB of the peak colour (the core centre)
};
static_assert(sizeof(Vertex) == 68, "the stage's vertex stride");

// The camera of the frame: world -> view rows (view_j = dot(p, rows[j].xyz) + rows[j].w), the projection's scale terms
// (the jitter terms move, they do not size) and the target height.
struct View {
    float rows[12]{};
    float m00 = 0.f, m11 = 0.f;
    float height = 0.f;
    float near_z = 1.f;
    x3m::renderer::FogTransmittanceLaw fog{}; // phase 3: off unless this frame's density composite applied
};
struct BuildStats {
    unsigned nozzles = 0, vertices = 0, discs = 0, steering = 0, capped = 0, faded = 0;
    unsigned culled_rows = 0, culled_behind = 0, culled_small = 0, culled_idle = 0, culled_capacity = 0;
    unsigned fogged = 0;        // nozzles whose colours took the fog transmittance (phase 3)
    float fog_min = 1.f;        // the smallest channel transmittance applied this frame
    unsigned skipped_other_view = 0;
};
// The per-record view tags beside the records (engine_effects_core.h Ring camera / scene) and the scene view's camera
// handle: a record is drawn when it was recorded in the scene phase with that handle.
struct ViewFilter {
    const std::uint32_t* camera = nullptr;
    const std::uint8_t* scene = nullptr;
    std::uint32_t handle = 0;
};
// The scene view's camera handle. Own rule first: the most frequent handle among the scene-phase records tagged as the
// own ship's jets (Ring::own; the own ship flies in the main view, so its camera is the scene view even when a target
// monitor's jets, recorded in the scene phase too, outnumber them). Majority rule only without such a record: the
// most frequent handle among all scene-phase records. Ties: the first seen; at most 8 distinct handles are tallied,
// later ones count against nothing. False when no record is in the scene phase. `own` may be null (no tags).
enum class ViewRule : std::uint8_t { none = 0, own = 1, majority = 2 };
inline const char* view_rule_name(ViewRule r) noexcept {
    return r == ViewRule::own ? "own" : r == ViewRule::majority ? "majority" : "none";
}
inline bool tally_scene_view(const std::uint32_t* camera, const std::uint8_t* scene, const std::uint8_t* own,
                             unsigned count, std::uint32_t* out) noexcept {
    std::uint32_t handles[8];
    unsigned votes[8], distinct = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!scene[i] || (own && !own[i])) continue;
        unsigned k = 0;
        while (k < distinct && handles[k] != camera[i]) ++k;
        if (k == distinct) {
            if (distinct == 8) continue;
            handles[distinct] = camera[i];
            votes[distinct++] = 0;
        }
        ++votes[k];
    }
    if (!distinct) return false;
    unsigned best = 0;
    for (unsigned k = 1; k < distinct; ++k)
        if (votes[k] > votes[best]) best = k;
    *out = handles[best];
    return true;
}
inline bool scene_view_camera(const std::uint32_t* camera, const std::uint8_t* scene, const std::uint8_t* own,
                              unsigned count, std::uint32_t* out, ViewRule* rule = nullptr) noexcept {
    ViewRule chosen = ViewRule::none;
    if (own && tally_scene_view(camera, scene, own, count, out))
        chosen = ViewRule::own;
    else if (tally_scene_view(camera, scene, nullptr, count, out))
        chosen = ViewRule::majority;
    if (rule) *rule = chosen;
    return chosen != ViewRule::none;
}

// The record's colours: the body's normalised mean / peak (engine_bodies.json mean_linear / peak_linear), else the
// cluster tint for both.
inline void record_tint(const ee::Record& r, const ee::Body* body, float mean[3], float peak[3]) noexcept {
    if (body && body->colour) {
        for (unsigned i = 0; i < 3; ++i) {
            mean[i] = body->mean[i];
            peak[i] = body->peak[i];
        }
        return;
    }
    const float* t = cluster_tint(unsigned(r.flags >> ee::cluster_shift) & 15u);
    for (unsigned i = 0; i < 3; ++i) mean[i] = peak[i] = t[i];
}
inline std::uint32_t pack_colour(const float c[3]) noexcept {
    std::uint32_t out = 0xff000000u;
    for (unsigned i = 0; i < 3; ++i) {
        float v = c[i];
        v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
        out |= std::uint32_t(int(v * 255.f + .5f)) << (16u - 8u * i);
    }
    return out;
}
// Slow value noise in [-1, 1] of (seed, frame): a hash per cell of flicker_frames frames, smoothstep between cells.
inline std::uint32_t hash32(std::uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline void flicker(std::uint32_t seed, std::uint32_t frame, float* out) noexcept {
    const std::uint32_t cell = frame / flicker_frames;
    const float f = float(frame % flicker_frames) * (1.f / float(flicker_frames));
    const float a = float(hash32(seed ^ hash32(cell)) >> 8) * (2.f / 16777216.f) - 1.f;
    const float b = float(hash32(seed ^ hash32(cell + 1u)) >> 8) * (2.f / 16777216.f) - 1.f;
    const float s = f * f * (3.f - 2.f * f);
    *out = 1.f + flicker_amplitude * (a + (b - a) * s);
}
inline std::uint32_t record_seed(const ee::Record& r) noexcept {
    return hash32(std::uint32_t(r.serial) ^ std::uint32_t(r.serial >> 32) ^ hash32(r.node_handle) ^ (r.model * 0x9e3779b9u));
}

namespace detail {
inline void to_view(const View& v, const float p[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j)
        out[j] = p[0] * v.rows[j * 4] + p[1] * v.rows[j * 4 + 1] + p[2] * v.rows[j * 4 + 2] + v.rows[j * 4 + 3];
}
inline void rotate(const View& v, const float d[3], float out[3]) noexcept {
    for (unsigned j = 0; j < 3; ++j) out[j] = d[0] * v.rows[j * 4] + d[1] * v.rows[j * 4 + 1] + d[2] * v.rows[j * 4 + 2];
}
inline void normalise(float d[3], float* length) noexcept {
    const float n = x3m::scalar::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    *length = n;
    if (n > 0.f)
        for (unsigned i = 0; i < 3; ++i) d[i] /= n;
}
inline void cross(const float a[3], const float b[3], float out[3]) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline bool finite3(const float* v) noexcept {
    return ee::finite_f(v[0]) && ee::finite_f(v[1]) && ee::finite_f(v[2]);
}
} // namespace detail

// Pixels per world unit at view depth z (vertical focal length over z).
inline void pixels_per_unit(const View& v, float z, float* out) noexcept {
    *out = v.m11 * v.height * .5f / (z > v.near_z ? z : v.near_z);
}

// The quad list's indices for `nozzles` nozzles: two quads of four vertices each, two triangles per quad.
inline void write_indices(std::uint16_t* out, unsigned nozzles) noexcept {
    for (unsigned q = 0; q < nozzles * 2u; ++q) {
        const std::uint16_t b = std::uint16_t(q * 4u);
        const std::uint16_t t[6] = {b, std::uint16_t(b + 1), std::uint16_t(b + 2), std::uint16_t(b + 2), std::uint16_t(b + 1),
                                    std::uint16_t(b + 3)};
        for (unsigned i = 0; i < 6; ++i) out[q * 6u + i] = t[i];
    }
}

// One nozzle's eight vertices. False: not drawn (stats says why).
inline bool build_nozzle(const ee::Record& r, const ee::Body* body, const View& view, float scale, std::uint32_t frame,
                         Vertex* out, BuildStats* stats) noexcept {
    if ((r.flags & ee::flag_rows_unknown) || !detail::finite3(r.origin) || !detail::finite3(r.axis) ||
        !ee::finite_f(r.size) || !(r.size > 0.f) || !ee::finite_f(r.z) || !ee::finite_f(r.s)) {
        ++stats->culled_rows;
        return false;
    }
    const bool steering = (r.flags & ee::flag_steering) != 0;
    if (steering && r.z < steering_min_z) {
        ++stats->culled_idle;
        return false;
    }
    float o[3], a[3], al = 0.f;
    detail::to_view(view, r.origin, o);
    detail::rotate(view, r.axis, a);
    detail::normalise(a, &al);
    if (!(al > 0.f) || !detail::finite3(o)) {
        ++stats->culled_rows;
        return false;
    }
    float value = r.size;
    float L = (r.z > 0.f ? r.z : 0.f) * value;
    float sigma = halo_sigma * value * scale;
    // Behind the camera: the whole axial quad (nozzle to tip, plus the halo reach) on the far side of the near plane.
    const float reach = halo_reach * sigma;
    const float tip_z = o[2] + a[2] * L;
    if ((o[2] < view.near_z && tip_z < view.near_z) && (o[2] + reach < view.near_z && tip_z + reach < view.near_z)) {
        ++stats->culled_behind;
        return false;
    }
    float ppu = 0.f;
    pixels_per_unit(view, o[2], &ppu);
    if (value * ppu < cull_px) {
        ++stats->culled_small;
        return false;
    }
    // The near-camera cap (the own ship in chase view): the plume's projected width, the halo's 1/e diameter 2 sigma at
    // the axis point nearest the camera (the tip when the exhaust approaches it), is held to 0.12 H by shrinking the
    // whole plume about the nozzle; its radiance fades 1 -> 0.5 over the last 20 % before the cap. Its length is
    // free: a distant capital's long plume is not shortened.
    float k = 1.f, near_weight = 1.f;
    {
        const float f = view.m11 * view.height * .5f, cap = chase_cap * view.height;
        const float toward = a[2] < 0.f ? -a[2] : 0.f; // approach to the camera per unit of length
        float near_depth = o[2] - toward * L;
        if (near_depth < view.near_z) near_depth = view.near_z;
        const float q = 2.f * sigma * f / near_depth / cap;
        if (q > 1.f - chase_fade_band) {
            float t = (q - (1.f - chase_fade_band)) * (1.f / chase_fade_band);
            t = t > 1.f ? 1.f : t;
            near_weight = 1.f - (1.f - chase_fade_floor) * t;
            ++stats->faded;
        }
        if (q > 1.f) {
            // Shrinking also moves the tip away: solve 2 sigma k f / (o_z - toward L k) = cap for k.
            const float denominator = 2.f * sigma * f + cap * toward * L;
            float kk = denominator > 0.f && o[2] > view.near_z ? cap * o[2] / denominator : 0.f;
            if (!(kk > 0.f) || kk > 1.f) kk = 1.f / q;
            k = kk;
            ++stats->capped;
        }
    }
    value *= k;
    L *= k;
    sigma *= k;
    // Minimum screen sizes (after the cap: a capped plume is large anyway).
    float r0 = core_radius * value;
    if (r0 * ppu < min_core_px) r0 = min_core_px / ppu;
    if (!steering && L * ppu < min_length_px) L = min_length_px / ppu;
    float disc = disc_radius * value;
    if (disc * ppu < min_core_px) disc = min_core_px / ppu;
    // Radiance: I(s) x preset, the core's slow flicker, the RCS weight z, the chase fade.
    const float s = r.s < 0.f ? 0.f : r.s > 1.f ? 1.f : r.s;
    float fl = 1.f;
    flicker(record_seed(r), frame, &fl);
    float weight = near_weight;
    if (steering) weight *= r.z < 1.f ? r.z : 1.f;
    const float i_core = (core_low + (core_high - core_low) * s) * scale * fl * weight;
    const float i_halo = (halo_low + (halo_high - halo_low) * s) * scale * weight;
    float mean[3], peak[3];
    record_tint(r, body, mean, peak);
    if (view.fog.on) {
        float t[3];
        x3m::renderer::fog_transmittance(view.fog, x3m::scalar::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]), t);
        for (unsigned i = 0; i < 3; ++i) {
            mean[i] *= t[i];
            peak[i] *= t[i];
            stats->fog_min = t[i] < stats->fog_min ? t[i] : stats->fog_min;
        }
        ++stats->fogged;
    }
    const std::uint32_t tint = pack_colour(mean), hot = pack_colour(peak);
    // Facing: e = unit vector from the nozzle to the camera; the axial quad's side n = a x e, a fallback when the axis
    // points along the line of sight.
    float e[3] = {-o[0], -o[1], -o[2]}, el = 0.f;
    detail::normalise(e, &el);
    if (!(el > 0.f)) {
        e[0] = e[1] = 0.f;
        e[2] = -1.f;
    }
    const float facing = a[0] * e[0] + a[1] * e[1] + a[2] * e[2]; // +1: the exhaust points at the camera
    const float bias = occlusion_bias * value * (facing > 0.f ? facing : 0.f);
    float n[3], nl = 0.f;
    detail::cross(a, e, n);
    detail::normalise(n, &nl);
    if (!(nl > 1e-4f)) {
        const float up[3] = {0.f, 1.f, 0.f}, right[3] = {1.f, 0.f, 0.f};
        detail::cross(a, a[1] * a[1] < .81f ? up : right, n);
        detail::normalise(n, &nl);
    }
    // The axial trapezoid: the halo window reaches 0 at 2.25 local sigma from the segment nozzle..tip (sigma tapering to
    // half at the tip), the core tapers from r0 to 0; the width is linear in u through (0, max(reach0, r0) + 1 px) and
    // (front, reach_tip + 1 px), which covers the nozzle's half disc, the side band and the tip's half disc.
    const float pixel = 1.f / ppu;
    const float reach0 = halo_reach * sigma, reach_tip = .5f * reach0;
    const float back = reach0 + pixel, front = L + reach_tip + pixel;
    const float width0 = (reach0 > r0 ? reach0 : r0) + pixel, width_front = reach_tip + pixel;
    const float width_back = width0 + (width0 - width_front) * back / front;
    const float corners[4][2] = {{-back, -width_back}, {-back, width_back}, {front, -width_front}, {front, width_front}};
    for (unsigned c = 0; c < 4; ++c) {
        Vertex& v = out[c];
        const float u = corners[c][0], w = corners[c][1];
        for (unsigned j = 0; j < 3; ++j) v.position[j] = o[j] + a[j] * u + n[j] * w;
        v.local[0] = u;
        v.local[1] = w;
        v.local[2] = L;
        v.local[3] = r0;
        v.shape[0] = sigma;
        v.shape[1] = value;
        v.shape[2] = bias;
        v.shape[3] = 0.f;
        v.intensity[0] = i_core;
        v.intensity[1] = i_halo;
        v.intensity[2] = a[2];
        v.intensity[3] = o[2];
        v.tint = tint;
        v.peak = hot;
    }
    // The disc, weighted by |a . e| and faded in over 0.15..0.3; under 0.15 it collapses to one point (no pixel).
    const float facing_abs = facing < 0.f ? -facing : facing;
    const bool disc_drawn = facing_abs >= disc_min_weight;
    float fade_in = (facing_abs - disc_min_weight) * (1.f / disc_fade_band);
    fade_in = fade_in < 0.f ? 0.f : fade_in > 1.f ? 1.f : fade_in;
    const float dw = facing_abs * fade_in;
    const float half = (halo_reach * sigma > disc + pixel ? halo_reach * sigma : disc + pixel) * (disc_drawn ? 1.f : 0.f);
    const float dc[4][2] = {{-half, -half}, {-half, half}, {half, -half}, {half, half}};
    for (unsigned c = 0; c < 4; ++c) {
        Vertex& v = out[4 + c];
        v.position[0] = o[0] + dc[c][0];
        v.position[1] = o[1] + dc[c][1];
        v.position[2] = o[2];
        v.local[0] = dc[c][0];
        v.local[1] = dc[c][1];
        v.local[2] = L;
        v.local[3] = disc;
        v.shape[0] = sigma;
        v.shape[1] = value;
        v.shape[2] = bias;
        v.shape[3] = 1.f;
        v.intensity[0] = i_core * dw;
        v.intensity[1] = i_halo * dw;
        v.intensity[2] = 0.f;
        v.intensity[3] = o[2];
        v.tint = tint;
        v.peak = hot;
    }
    if (disc_drawn) ++stats->discs;
    if (steering) ++stats->steering;
    return true;
}

// The frame's records into `out` (capacity in nozzles); returns the nozzles written (8 vertices each). `body` maps a
// record's table index to its entry (null: none); `filter` (null: every record) keeps the scene view's records.
using BodyLookup = const ee::Body* (*)(int index);
inline unsigned build(const ee::Record* records, unsigned count, BodyLookup body, const View& view, Preset preset,
                      std::uint32_t frame, Vertex* out, unsigned capacity, BuildStats* stats,
                      const ViewFilter* filter = nullptr) noexcept {
    BuildStats local{};
    BuildStats& st = stats ? *stats : local;
    st = BuildStats{};
    if (!records || !out || !(view.m11 > 0.f) || !(view.height > 0.f) || !(view.near_z > 0.f)) return 0;
    for (unsigned i = 0; i < 12; ++i)
        if (!ee::finite_f(view.rows[i])) return 0;
    float scale = 1.f;
    preset_scale(preset, &scale);
    unsigned written = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (written >= capacity || written >= max_nozzles) {
            st.culled_capacity += count - i;
            break;
        }
        if (filter && (!filter->scene[i] || filter->camera[i] != filter->handle)) {
            ++st.skipped_other_view;
            continue;
        }
        const ee::Record& r = records[i];
        const ee::Body* b = body && r.body >= 0 ? body(r.body) : nullptr;
        if (build_nozzle(r, b, view, scale, frame, out + written * vertices_per_nozzle, &st)) ++written;
    }
    st.nozzles = written;
    st.vertices = written * vertices_per_nozzle;
    return written;
}
} // namespace x3m::engine_plumes
