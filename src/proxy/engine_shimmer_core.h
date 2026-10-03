#pragma once
#include <cstddef>
#include <cstdint>
#include "engine_plumes_core.h"

// Portable core of the engine heat shimmer (docs/architecture/engine-exhaust-gap-analysis.md gap 9, phase 5;
// docs/architecture/effects-modernisation-opus.md section 3.8): the option parsers, the Ctrl+Alt+F7 toggle latch, the
// screen rects behind the frame's nozzles and the pixel program's constant block and quads. No Windows dependency (the
// host tests compile it); float work through SSE scalars, no float returned by value.
//
// Rects: per scene-view record the plume builder itself (engine_plumes::build_nozzle, the same view, look, preset,
// clock and plume floor as the stage) places the axial quad; its first four corners give back the nozzle's view
// position o, the unit axis a, the pulsed length L and the nozzle width n after the near-camera cap. A nozzle whose
// projected width n x ppu is under gate_px (24 px) gets no shimmer; the rest are ranked by that width (nearest and
// largest first) and the first `limit` kept (engine_shimmer_max, default 4, at most max_rects 16). The rect is oriented along the projected axis: from back pixels
// behind the nozzle (a quarter nozzle width; more when the plume is foreshortened, so an end-on plume's rect is centred
// on the nozzle) to length = max(|P(o + 1.5 L a) - P(o)|, half_width) pixels ahead, half_width = one projected nozzle
// width either side (the rect is two nozzle widths wide). Points are projected with the unjittered projection (the
// resolved image is on the unjittered grid), the tip cut at the near plane. Pixel coordinates are continuous: pixel i
// covers [i, i + 1], its centre i + 0.5 (D3D9 maps NDC x to window x = (x + 1) W / 2 at the pixel centre, so the
// continuous coordinate is that plus 0.5).
//
// The mask (the pixel program, src/effects/engine_shimmer_ps.hlsl): smoothstep(-back, 0, s) x (1 - smoothstep(0,
// length, s)) along the axis (1 just behind the nozzle, 0 at 1.5 L) x (1 - smoothstep(0.35, 1, |t| / half_width))
// across; 0 on the rect's border, so the drawn quads (the rect plus one pixel) have no seam. Occlusion: a scene depth
// in [0, depth) (nearer than the plume's nearest point less one nozzle width; the -1 sky never) takes no shimmer, so a
// hull in front of its own exhaust is not distorted.
namespace x3m::engine_shimmer {
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;

constexpr unsigned max_rects = 16;             // the program's capacity: engine_shimmer_max's upper bound
constexpr unsigned default_max = 4;            // X3M_ENGINE_SHIMMER_MAX: the own ship plus the nearest (bounds the cost)
constexpr float gate_px = 24.f;                // projected nozzle width under which a nozzle gets none
constexpr float length_factor = 1.5f;          // the rect's reach along the axis, x L
constexpr float half_width_widths = 1.f;       // the rect's half-width in nozzle widths (two nozzle widths wide)
constexpr float back_widths = .25f;            // the fade-in behind the nozzle, nozzle widths
constexpr float side_inner = .35f;             // the across fade starts at this share of the half-width
constexpr float occlusion_margin_widths = 1.f; // nozzle widths nearer than the plume's nearest point
constexpr float cell_widths = .5f;             // the noise cell, nozzle widths
constexpr float boil_rate = 1.5f;              // the noise's third axis per second (the flow scrolls the first)
constexpr float default_px = 1.5f, px_min = 0.f, px_max = 4.f; // X3M_ENGINE_SHIMMER_PX: amplitude at 1440 rows
constexpr float reference_rows = 1440.f;       // the amplitude scales with the target height
constexpr float max_length_screens = 4.f;      // a tip barely past the near plane: the rect's length is held here
constexpr unsigned constant_vectors = 4 + 3 * max_rects; // c0..c51
constexpr unsigned vertices_per_rect = 6;      // two triangles (TRIANGLELIST)

// ----------------------------------------------------------------------------------------------------- options
// X3M_ENGINE_SHIMMER: exactly "on" or "off".
template <class Char> inline bool parse_mode(const Char* text, std::size_t n, bool* on) noexcept {
    if (!text || !on) return false;
    if (n == 2 && text[0] == Char('o') && text[1] == Char('n')) {
        *on = true;
        return true;
    }
    if (n == 3 && text[0] == Char('o') && text[1] == Char('f') && text[2] == Char('f')) {
        *on = false;
        return true;
    }
    return false;
}
// X3M_ENGINE_SHIMMER_MAX: one plain integer in 0..max_rects (digits only, at most two).
template <class Char> inline bool parse_max(const Char* text, std::size_t n, unsigned* out) noexcept {
    if (!text || !out || n < 1 || n > 2) return false;
    unsigned v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (text[i] < Char('0') || text[i] > Char('9')) return false;
        v = v * 10u + unsigned(text[i] - Char('0'));
    }
    if (v > max_rects) return false;
    *out = v;
    return true;
}
// X3M_ENGINE_SHIMMER_PX: one plain decimal in 0..4 (pixels at 1440 rows).
template <class Char> inline bool parse_px(const Char* text, std::size_t n, float* out) noexcept {
    return ep::parse_plain_decimal(text, n, px_min, px_max, out);
}
// The amplitude in pixels of a target `height` rows high: px x height / 1440.
inline void amplitude_px(float px, float height, float* out) noexcept {
    *out = px > 0.f && height > 0.f ? px * height / reference_rows : 0.f;
}

// Ctrl+Alt+F7 (Shift up, focused): the press edge of F7. A held F7 never becomes a press by changing modifiers.
struct ToggleKey {
    bool f7_down = false;
    bool step(bool focused, bool control, bool alt, bool shift, bool f7) noexcept {
        const bool press = focused && control && alt && !shift && f7 && !f7_down;
        f7_down = f7;
        return press;
    }
};

// ----------------------------------------------------------------------------------------------------- rects
// The unjittered projection of the resolved image and the depth law of the lane (z_device = m22 + m32 / z).
struct Projection {
    float m00 = 0.f, m11 = 0.f, m20 = 0.f, m21 = 0.f, m22 = 0.f, m32 = 0.f;
    float width = 0.f, height = 0.f;
};
struct Rect {
    float origin[2]{};     // the nozzle, continuous pixels
    float axis[2]{};       // unit screen direction of the plume
    float length = 0.f;    // pixels ahead of the nozzle (>= half_width)
    float half_width = 0.f;
    float back = 0.f;      // pixels behind the nozzle
    float nozzle_px = 0.f; // projected nozzle width (the rank)
    float seed = 0.f;      // 0..1
    float depth = 0.f;     // occlusion: a scene device depth in [0, depth) takes no shimmer (0: none)
    float phase = 0.f;     // the nozzle's own flow phase, nozzle widths (engine_plumes::nozzle_phase, as its plume)
    float corners[4][2]{}; // the drawn quad (the rect plus one pixel): back-left, back-right, front-left, front-right
    int bounds[4]{};       // x0, y0, x1, y1 (exclusive), the quad's box clipped to the target
};
struct Stats {
    unsigned candidates = 0; // scene-view records
    unsigned behind = 0;     // nozzle at or behind the near plane
    unsigned small = 0;      // projected nozzle width under gate_px
    unsigned refused = 0;    // the builder drew none (invalid rows, idle RCS, culled)
    unsigned offscreen = 0;  // the quad misses the target
    unsigned capped = 0;     // past the limit
    unsigned kept = 0;
};

namespace detail {
inline void project(const Projection& p, const float v[3], float out[2]) noexcept {
    const float x = p.m00 * v[0] / v[2] + p.m20, y = p.m11 * v[1] / v[2] + p.m21;
    out[0] = (x + 1.f) * p.width * .5f + .5f;
    out[1] = (1.f - y) * p.height * .5f + .5f;
}
inline bool finite2(const float* v) noexcept {
    return ee::finite_f(v[0]) && ee::finite_f(v[1]);
}
inline int floor_int(float v) noexcept {
    return int(x3m::scalar::floor(double(v)));
}
inline int ceil_int(float v) noexcept {
    return int(x3m::scalar::ceil(double(v)));
}
} // namespace detail

// A rect from its screen terms: the drawn quad (the rect plus one pixel on every side) and its box clipped to the target
// (width x height). False: the quad misses the target.
inline bool make_rect(const float origin[2], const float axis[2], float length, float half_width, float back, float nozzle_px,
                      float seed, float depth, float width, float height, Rect* out) noexcept {
    Rect r{};
    r.origin[0] = origin[0];
    r.origin[1] = origin[1];
    r.axis[0] = axis[0];
    r.axis[1] = axis[1];
    r.length = length;
    r.half_width = half_width;
    r.back = back;
    r.nozzle_px = nozzle_px;
    r.seed = seed;
    r.depth = depth;
    const float s0 = -back - 1.f, s1 = length + 1.f, t = half_width + 1.f;
    const float perp[2] = {-axis[1], axis[0]};
    const float ss[4] = {s0, s0, s1, s1}, tt[4] = {-t, t, -t, t};
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (unsigned c = 0; c < 4; ++c)
        for (unsigned j = 0; j < 2; ++j) {
            const float value = origin[j] + axis[j] * ss[c] + perp[j] * tt[c];
            r.corners[c][j] = value;
            lo[j] = value < lo[j] ? value : lo[j];
            hi[j] = value > hi[j] ? value : hi[j];
        }
    const float limit[2] = {width, height};
    for (unsigned j = 0; j < 2; ++j) {
        lo[j] = lo[j] < 0.f ? 0.f : lo[j];
        hi[j] = hi[j] > limit[j] ? limit[j] : hi[j];
    }
    if (!(hi[0] > lo[0]) || !(hi[1] > lo[1])) return false;
    r.bounds[0] = detail::floor_int(lo[0]);
    r.bounds[1] = detail::floor_int(lo[1]);
    r.bounds[2] = detail::ceil_int(hi[0]);
    r.bounds[3] = detail::ceil_int(hi[1]);
    if (r.bounds[2] <= r.bounds[0] || r.bounds[3] <= r.bounds[1]) return false;
    *out = r;
    return true;
}

// One built nozzle (the stage's eight vertices) -> its rect. False: under the gate, behind, or off the target.
inline bool rect_of(const ep::Vertex* v, const ep::View& view, const Projection& p, float seed, Rect* out, Stats* st) noexcept {
    // The axial quad's corners: v0/v1 = o - a back -/+ side w_back, v2/v3 = o + a front -/+ side w_front, with
    // local[0] = -back / front.
    const float back = -v[0].local[0], front = v[2].local[0];
    if (!(front + back > 0.f)) {
        ++st->refused;
        return false;
    }
    float o[3], a[3];
    for (unsigned j = 0; j < 3; ++j) {
        const float m01 = .5f * (v[0].position[j] + v[1].position[j]), m23 = .5f * (v[2].position[j] + v[3].position[j]);
        a[j] = (m23 - m01) / (front + back);
        o[j] = m01 + a[j] * back;
    }
    const float L = v[0].local[2], n = v[0].local[3];
    if (!(o[2] > view.near_z) || !ep::detail::finite3(o) || !ep::detail::finite3(a)) {
        ++st->behind;
        return false;
    }
    float ppu = 0.f;
    ep::pixels_per_unit(view, o[2], &ppu);
    const float nozzle_px = n * ppu;
    if (!(nozzle_px >= gate_px)) {
        ++st->small;
        return false;
    }
    // The tip at 1.5 L, cut at the near plane (just in front of it).
    float reach = length_factor * (L > 0.f ? L : 0.f);
    const float near_cut = view.near_z * 1.001f;
    if (o[2] + a[2] * reach < near_cut && a[2] < 0.f) reach = (near_cut - o[2]) / a[2];
    if (!(reach > 0.f)) reach = 0.f;
    const float tip[3] = {o[0] + a[0] * reach, o[1] + a[1] * reach, o[2] + a[2] * reach};
    float p0[2], p1[2];
    detail::project(p, o, p0);
    detail::project(p, tip, p1);
    if (!detail::finite2(p0) || !detail::finite2(p1)) {
        ++st->behind;
        return false;
    }
    float d[2] = {p1[0] - p0[0], p1[1] - p0[1]};
    float len = x3m::scalar::sqrt(d[0] * d[0] + d[1] * d[1]);
    const float longest = max_length_screens * (p.width > p.height ? p.width : p.height);
    if (len > 1e-3f) {
        d[0] /= len;
        d[1] /= len;
    } else {
        d[0] = 1.f;
        d[1] = 0.f;
        len = 0.f;
    }
    if (len > longest) len = longest;
    const float hw = half_width_widths * nozzle_px;
    float back_px = back_widths * nozzle_px;
    if (len < hw && hw - len > back_px) back_px = hw - len; // foreshortened: centred on the nozzle
    const float ahead = len > hw ? len : hw;
    // Occlusion: the plume's nearest point less one nozzle width, as a device depth.
    float z_near = o[2] < tip[2] ? o[2] : tip[2];
    z_near -= occlusion_margin_widths * n;
    if (z_near < view.near_z) z_near = view.near_z;
    float depth = p.m22 + p.m32 / z_near;
    depth = ee::finite_f(depth) && depth > 0.f && p.m32 < 0.f ? depth : 0.f;
    if (!make_rect(p0, d, ahead, hw, back_px, nozzle_px, seed, depth, p.width, p.height, out)) {
        ++st->offscreen;
        return false;
    }
    return true;
}

// The frame's rects (at most `limit` (<= max_rects) into `out`, ranked by projected nozzle width, largest first; ties keep record
// order). The inputs are the plume stage's (engine_plumes::build): records, body lookup, view, preset, the stage's clock,
// the view filter, look, tables and radii (null: no plume floor), and its dynamics (the flow accumulator and the SETA
// travel weight; its attack memory is never touched: the builder runs here without it, so the stage's transients and the
// rects' geometry agree, the radiance aside); `p` the unjittered projection and the target size.
inline unsigned collect(const ee::Record* records, unsigned count, ep::BodyLookup body, const ep::View& view,
                        const Projection& p, ep::Preset preset, float seconds, const ep::ViewFilter* filter,
                        const ep::Look* look, const ep::LookTables* tables, const float* radii, Rect* out,
                        Stats* stats, unsigned limit = max_rects, const ep::Dynamics* dynamics = nullptr) noexcept {
    Stats local{};
    Stats& st = stats ? *stats : local;
    st = Stats{};
    if (!records || !out || !(view.m11 > 0.f) || !(view.height > 0.f) || !(view.near_z > 0.f) || !(p.m00 > 0.f) ||
        !(p.m11 > 0.f) || !(p.width > 0.f) || !(p.height > 0.f))
        return 0;
    for (unsigned i = 0; i < 12; ++i)
        if (!ee::finite_f(view.rows[i])) return 0;
    if (!ee::finite_f(seconds)) seconds = 0.f;
    const ep::Look& k = look ? *look : ep::default_look;
    float scale = 1.f;
    ep::preset_scale(preset, &scale);
    ep::LookTables computed;
    if (!tables) {
        ep::look_tables(k, &computed);
        tables = &computed;
    }
    if (limit > max_rects) limit = max_rects;
    ep::Dynamics geometry{};
    if (dynamics) {
        geometry = *dynamics;
        geometry.transients = nullptr;
    }
    const ep::Dynamics* dyn = dynamics ? &geometry : nullptr;
    constexpr std::uint32_t unfloored = ee::flag_steering | ee::flag_brake;
    const bool floors = radii && k.floor_scale > 0.f && ee::finite_f(k.floor_scale);
    ep::PulseCache pulses;
    unsigned kept = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (filter && filter->scene && filter->camera && !(filter->scene[i] && filter->camera[i] == filter->handle)) continue;
        ++st.candidates;
        const ee::Record& r = records[i];
        if (!ep::detail::finite3(r.origin) || !ee::finite_f(r.size) || !(r.size > 0.f)) {
            ++st.refused;
            continue;
        }
        // The cheap gate first: the nozzle's view depth and its widest possible projected width (the floored value;
        // the near-camera cap only shrinks it); the builder runs only for the survivors.
        float o[3];
        ep::detail::to_view(view, r.origin, o);
        if (!(o[2] > view.near_z)) {
            ++st.behind;
            continue;
        }
        // The floor raises a main jet to at most floor_cap x its value: that bound gates before the floor's logarithms.
        const bool floored = floors && !(r.flags & unfloored);
        float ppu = 0.f;
        ep::pixels_per_unit(view, o[2], &ppu);
        const float widest = floored && k.floor_cap > 1.f ? k.floor_cap * r.size : r.size;
        if (!(k.nozzle_width * widest * ppu >= gate_px)) {
            ++st.small;
            continue;
        }
        float floor_value = 0.f;
        if (floored) {
            const float radius = radii[i];
            if (radius > 0.f && ee::finite_f(radius)) ep::floor_target(k, r.size, radius, &floor_value);
        }
        const float value = floor_value > r.size ? floor_value : r.size;
        if (!(k.nozzle_width * value * ppu >= gate_px)) {
            ++st.small;
            continue;
        }
        ep::Vertex v[ep::vertices_per_nozzle];
        ep::BuildStats bs{};
        const ee::Body* b = body && r.body >= 0 ? body(r.body) : nullptr;
        if (!ep::build_nozzle(r, b, view, k, *tables, scale, seconds, floor_value, v, &bs, &pulses, dyn)) {
            ++st.refused;
            continue;
        }
        Rect rect{};
        if (!rect_of(v, view, p, float(ep::seed_byte(r)) * (1.f / 255.f), &rect, &st)) continue;
        {
            float factor = 1.f;
            ep::flow_factor(k, value, &factor);
            ep::nozzle_phase(dynamics ? dynamics->flow : 0., factor, &rect.phase);
        }
        // Insert by rank (largest projected nozzle first); a full list drops its smallest.
        unsigned at = kept;
        while (at > 0 && out[at - 1].nozzle_px < rect.nozzle_px) --at;
        if (at >= limit) {
            ++st.capped;
            continue;
        }
        if (kept == limit) ++st.capped; // the last one falls off
        const unsigned last = kept < limit ? kept : limit - 1;
        for (unsigned j = last; j > at; --j) out[j] = out[j - 1];
        out[at] = rect;
        if (kept < limit) ++kept;
    }
    st.kept = kept;
    return kept;
}

// The union of the rects' boxes (x0, y0, x1, y1), grown by `margin` pixels and clipped to the target. False: none.
inline bool union_bounds(const Rect* rects, unsigned n, int margin, int width, int height, int out[4]) noexcept {
    if (!rects || !n || width <= 0 || height <= 0) return false;
    int b[4] = {rects[0].bounds[0], rects[0].bounds[1], rects[0].bounds[2], rects[0].bounds[3]};
    for (unsigned i = 1; i < n; ++i) {
        b[0] = rects[i].bounds[0] < b[0] ? rects[i].bounds[0] : b[0];
        b[1] = rects[i].bounds[1] < b[1] ? rects[i].bounds[1] : b[1];
        b[2] = rects[i].bounds[2] > b[2] ? rects[i].bounds[2] : b[2];
        b[3] = rects[i].bounds[3] > b[3] ? rects[i].bounds[3] : b[3];
    }
    b[0] -= margin;
    b[1] -= margin;
    b[2] += margin;
    b[3] += margin;
    out[0] = b[0] < 0 ? 0 : b[0];
    out[1] = b[1] < 0 ? 0 : b[1];
    out[2] = b[2] > width ? width : b[2];
    out[3] = b[3] > height ? height : b[3];
    return out[2] > out[0] && out[3] > out[1];
}

// The pixel program's constants (src/effects/engine_shimmer_ps.hlsl): c0 (1/W, 1/H, W, H), c1 (amplitude px, rect
// count, 0, boil), c2 (1 / cell, 0, 0, 0), c3 zero; per rect i: c4+i (origin, axis), c20+i (length,
// half-width, back, 1 / nozzle px), c36+i (seed, depth, flow phase / cell, 0). Unused rects stay zero (the program
// skips them past the count). `seconds` the stage's clock.
inline void constants(const Rect* rects, unsigned n, float amplitude, float seconds, float width, float height,
                      float out[constant_vectors * 4]) noexcept {
    for (unsigned i = 0; i < constant_vectors * 4; ++i) out[i] = 0.f;
    if (n > max_rects) n = max_rects;
    float* c = out;
    c[0] = width > 0.f ? 1.f / width : 0.f;
    c[1] = height > 0.f ? 1.f / height : 0.f;
    c[2] = width;
    c[3] = height;
    c[4] = amplitude;
    c[5] = float(n);
    // The phases and the clock wrap (nozzle_phase at 4,096 nozzle widths, the clock at 1,024 s): one jump of the field
    // per wrap, as in the plume.
    c[6] = 0.f;
    c[7] = seconds * boil_rate;
    c[8] = 1.f / cell_widths;
    for (unsigned i = 0; i < n; ++i) {
        const Rect& r = rects[i];
        float* a = out + (4 + i) * 4;
        float* b = out + (4 + max_rects + i) * 4;
        float* e = out + (4 + 2 * max_rects + i) * 4;
        a[0] = r.origin[0];
        a[1] = r.origin[1];
        a[2] = r.axis[0];
        a[3] = r.axis[1];
        b[0] = r.length;
        b[1] = r.half_width;
        b[2] = r.back;
        b[3] = r.nozzle_px > 0.f ? 1.f / r.nozzle_px : 0.f;
        e[0] = r.seed * 61.7f;
        e[1] = r.depth;
        e[2] = r.phase / cell_widths;
    }
}

// The quads: six vertices per rect (TRIANGLELIST), the quad vertex layout of renderer/quad_vertex_program.h (clip x, y,
// 0, 1, then u, v): a continuous pixel coordinate (X, Y) maps to clip (2 X / W - 1 - 1 / W, 1 - 2 Y / H + 1 / H) and
// uv (X / W, Y / H), so pixel i's centre interpolates u = (i + 0.5) / W (the -0.5 pixel shift of every proxy quad).
struct QuadVertex {
    float x, y, z, w, u, v;
};
static_assert(sizeof(QuadVertex) == 24, "the quad vertex stride");
inline void vertices(const Rect* rects, unsigned n, float width, float height, QuadVertex* out) noexcept {
    const float iw = 1.f / width, ih = 1.f / height;
    static constexpr unsigned order[vertices_per_rect] = {0, 1, 2, 2, 1, 3};
    for (unsigned i = 0; i < n; ++i)
        for (unsigned k = 0; k < vertices_per_rect; ++k) {
            const float* c = rects[i].corners[order[k]];
            QuadVertex& q = out[i * vertices_per_rect + k];
            q.x = 2.f * c[0] * iw - 1.f - iw;
            q.y = 1.f - 2.f * c[1] * ih + ih;
            q.z = 0.f;
            q.w = 1.f;
            q.u = c[0] * iw;
            q.v = c[1] * ih;
        }
}

// The mask of rect `r` at continuous pixel p (the pixel program's law; host tests and the fixture's replica).
inline void smooth01(float e0, float e1, float x, float* out) noexcept {
    float t = e1 > e0 ? (x - e0) / (e1 - e0) : (x >= e1 ? 1.f : 0.f);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    *out = t * t * (3.f - 2.f * t);
}
inline void mask_at(const Rect& r, float px, float py, float* out) noexcept {
    const float rx = px - r.origin[0], ry = py - r.origin[1];
    const float s = rx * r.axis[0] + ry * r.axis[1], t = -rx * r.axis[1] + ry * r.axis[0];
    const float at = t < 0.f ? -t : t;
    *out = 0.f;
    if (!(s > -r.back) || !(s < r.length) || !(at < r.half_width)) return;
    float in = 0.f, along = 0.f, side = 0.f;
    smooth01(-r.back, 0.f, s, &in);
    smooth01(0.f, r.length, s, &along);
    smooth01(side_inner * r.half_width, r.half_width, at, &side);
    *out = in * (1.f - along) * (1.f - side);
}
} // namespace x3m::engine_shimmer
