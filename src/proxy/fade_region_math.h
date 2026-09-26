#pragma once
#include "sse_scalar.h"
#include <cmath>
#include <cstdint>

// Conservative screen rectangle of an object-space AABB projected through the
// four clip rows a draw actually uses (docs/architecture/linear-distance-fade-region.md,
// section 2). Pure functions, no Windows or D3D dependency: shared by the
// runtime (src/proxy/fade_region.cpp), the detached fade fixture and the host
// test driver (verification/probe/fade_region_host.cpp), so the fixture and the
// test exercise exactly the production arithmetic.
namespace x3m::fade_region {

struct Box {
    double centre[3]{};
    double half[3]{}; // half-extents in POSITION0 units, before the half-float expansion
};
// D3D RECT convention: left/top inclusive, right/bottom exclusive, in target pixels.
struct Rect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};
struct Viewport {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};
// Why a draw did not get a box-derived rectangle; every value but Bound means
// the full viewport. Ordered for the counters and the fade_region log field.
enum class Reason : unsigned {
    Bound = 0,        // rectangle derived from the box
    Viewport = 1,     // application viewport unknown or empty
    Rows = 2,         // clip rows unknown
    BoundUnknown = 3, // no bound (no scope, table miss, mismatch, poison, negative extent)
    NonPositiveW = 4, // some corner has clip w <= 0: the projected hull is not convex
    NonFinite = 5,    // nonfinite row, box or projection
    FillMode = 6,     // FILLMODE unknown or not SOLID: a line or point fill may exceed the hull
    BehindNear = 7,   // near clipping: every corner has clip z < 0, nothing of the box is visible
    Count = 8
};

// POSITION0 is stored as FLOAT16_4 while the AABB was accumulated from the
// int16 source values; one half-float ULP in [1, 2] is 2^-10, which bounds
// any rounding of the whole |p| <= 2 domain (note, section 2).
constexpr double half_float_expansion = 1.0 / 1024.0;

// Conservative pad, in pixels, for the fp32 `dp4` the GPU runs on the same
// rows and points (docs/architecture/screen-emission-bullet-bound.md, section
// 4). We project in double precision; the hardware does not, so a projected
// hull vertex can land up to the fp32 rounding of its own dot product away
// from where the rasteriser puts it.
//
//   Each clip component is a 4-term dot product: rounding it in fp32 costs at
//   most 4 * 2^-24 * sum|terms| = 2^-22 * sum|terms| = eps_dp4 * S (no FMA
//   contraction assumed; one contracted product stays inside the same bound).
//   Cancellation is what makes S large: a bullet at world coordinate 1.2e5
//   against a view translation of -1.2e5 has S ~ 2.4e5 even though the clip
//   value is small. The note's calibration: S ~ 1.26e5 gives 0.03 clip units.
//   Screen error: sx = half_w * (x/w + 1), so
//     |dsx| <= half_w * (|dx| + |x/w| * |dw|) / w <= 2 * half * eps_dp4 * S / w
//   for |x/w| <= 1 (a point outside the viewport needs no pad: the rectangle
//   is clamped to the viewport anyway). Hence pad_px = ceil(k / w_min) with
//   k = 2 * half * eps_dp4 * S and w_min the smallest clip w of the (clipped)
//   polytope's vertices. At the note's numbers and half = 640 px this is
//   ~3 px at w = 6 and under 1 px at w >= 100, matching section 4.
//
// Never below the historical 1 px (the rectangle only ever grows) and capped
// at pad_limit so a degenerate w near zero cannot inflate the rectangle; the
// viewport intersection bounds it in any case. The cap is not a hole in the
// bound for the routes that use it: the near cut keeps w_min >= the game's zn
// (6 in gameplay), where k / w stays well under pad_limit for the bullet
// magnitudes; the host oracle reports cap-truncated cases separately
// (verification/probe/fade_region_host.cpp, --near).
constexpr double eps_dp4 = 1.0 / 4194304.0; // 4 * 2^-24
constexpr unsigned pad_limit = 8;
inline unsigned pad_pixels(double term_sum_max, double w_min, double half_max) noexcept {
    const double k = 2.0 * half_max * eps_dp4 * term_sum_max;
    if (!(k > 0) || !(w_min > 0)) return w_min > 0 ? 1u : pad_limit; // nonfinite or degenerate w
    const double q = k / w_min;
    if (!(q > 1)) return 1u;                         // covers NaN (ceil(q) <= 1)
    if (q > double(pad_limit - 1)) return pad_limit; // ceil(q) >= pad_limit
    return unsigned(scalar::ceil(q));                // 2..pad_limit-1; sse_scalar.h: the CRT ceil returns in st(0)
}

// MotionOutput::apply_jitter's arithmetic, bit for bit (single precision,
// SSE): rows[0] += jx_ndc * rows[3], rows[1] += jy_ndc * rows[3].
inline void jitter_rows(float rows[16], float jx_px, float jy_px, unsigned width, unsigned height) noexcept {
    const float jx = 2.f * jx_px / float(width), jy = -2.f * jy_px / float(height);
    for (unsigned k = 0; k < 4; ++k) {
        rows[k] += jx * rows[12 + k];
        rows[4 + k] += jy * rows[12 + k];
    }
}

inline Rect full_rect(const Viewport& v) noexcept {
    return {std::int32_t(v.x), std::int32_t(v.y), std::int32_t(v.x + v.width), std::int32_t(v.y + v.height)};
}
inline bool empty(const Rect& r) noexcept {
    return r.right <= r.left || r.bottom <= r.top;
}
inline Rect intersect(const Rect& a, const Rect& b) noexcept {
    Rect r{a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top, a.right < b.right ? a.right : b.right,
           a.bottom < b.bottom ? a.bottom : b.bottom};
    if (empty(r)) r = {0, 0, 0, 0};
    return r;
}
inline std::uint64_t area(const Rect& r) noexcept {
    return empty(r) ? 0u : std::uint64_t(r.right - r.left) * std::uint64_t(r.bottom - r.top);
}
inline double area_fraction(const Rect& r, const Viewport& v) noexcept {
    const double whole = double(v.width) * double(v.height);
    return whole > 0 ? double(area(r)) / whole : 1.0;
}
inline bool contains(const Rect& r, std::int32_t x, std::int32_t y) noexcept {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// Near-plane clipping (screen-emission-region.md, step B). D3D rasterises
// only clip-space points with 0 <= z <= w, so the box is cut against the
// plane z = 0 (the game's zn, 6 in gameplay, is where the projection rows put
// it; nothing is hard-coded) before the perspective divide: the polytope
// box ∩ {z >= 0} is convex, its vertices are the corners with z >= 0 plus the
// intersections of the 12 box edges that cross the plane, and with w > 0 at
// every one of them its projection is the convex hull of their projections.
// A box with no corner at z >= 0 is BehindNear (nothing visible). A vertex
// with w <= 0 after the cut (rows that are not a perspective projection)
// stays NonPositiveW. `clipped` receives the number of corners cut away
// (0: the plain eight-corner projection).
struct NearClip {
    bool enabled = false;
    unsigned clipped = 0;
};

// Eight corners of the expanded box through the rows, in double precision:
// clip = (r0.p, r1.p, r2.p, r3.p) with p = (x, y, z, 1) and row k at rows[4k..4k+3]
// (the VS's dp4 oPos.k, v0, c[base+k]). Every corner must have finite clip
// values and w > 0; then sx = X + (x/w + 1) W/2, sy = Y + (1 - y/w) H/2, the
// rectangle [floor(min) - pad, ceil(max) + pad] inclusive on both axes
// with the w-scaled pad above (>= the historical 1 px),
// intersected with the viewport. An empty intersection yields the 1x1
// rectangle at the viewport origin. Returns Reason::Bound and writes *out;
// any other reason leaves *out untouched and the caller uses the full viewport.
inline Reason project_box(const float rows[16], const Box& box, const Viewport& viewport, Rect* out,
                          NearClip* cut = nullptr, unsigned* pad_out = nullptr) noexcept {
    if (!viewport.width || !viewport.height) return Reason::Viewport;
    for (unsigned i = 0; i < 16; ++i)
        if (!std::isfinite(rows[i])) return Reason::NonFinite;
    for (unsigned a = 0; a < 3; ++a) {
        if (!std::isfinite(box.centre[a]) || !std::isfinite(box.half[a])) return Reason::NonFinite;
        if (!(box.half[a] >= 0)) return Reason::BoundUnknown;
    }
    double clip[8][4];
    // Largest |term| sum over the rows that reach the screen (x, y and w) and
    // over the corners: the fp32 `dp4` error bound above. A near-plane
    // crossing is a convex combination of two corners, and every term is
    // affine in the point, so the corner maximum bounds the crossings too.
    double term_sum_max = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        double p[3];
        for (unsigned a = 0; a < 3; ++a) {
            const double extent = box.half[a] + half_float_expansion;
            p[a] = box.centre[a] + ((corner >> a) & 1u ? extent : -extent);
        }
        for (unsigned k = 0; k < 4; ++k) {
            const float* row = rows + 4 * k;
            clip[corner][k] = double(row[0]) * p[0] + double(row[1]) * p[1] + double(row[2]) * p[2] + double(row[3]);
            if (!std::isfinite(clip[corner][k])) return Reason::NonFinite;
            if (k == 2) continue; // the z row never reaches a screen coordinate
            const double sum = scalar::abs(double(row[0]) * p[0]) + scalar::abs(double(row[1]) * p[1]) +
                               scalar::abs(double(row[2]) * p[2]) + scalar::abs(double(row[3]));
            if (sum > term_sum_max) term_sum_max = sum;
        }
        // Unclipped: refuse at the first corner with w <= 0, before the later
        // corners are evaluated (the fade route's reason histogram unchanged).
        if (!(cut && cut->enabled) && !(clip[corner][3] > 0)) return Reason::NonPositiveW;
    }
    const bool clipping = cut && cut->enabled;
    if (cut) cut->clipped = 0;
    if (clipping) {
        unsigned behind = 0;
        for (unsigned corner = 0; corner < 8; ++corner)
            if (clip[corner][2] < 0) ++behind;
        cut->clipped = behind;
        if (behind == 8) return Reason::BehindNear;
    }
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0, w_min = 0;
    const double X = double(viewport.x), Y = double(viewport.y);
    const double half_w = double(viewport.width) * 0.5, half_h = double(viewport.height) * 0.5;
    bool first = true;
    // One projected vertex of the (clipped) polytope; NonPositiveW or
    // NonFinite refuse the whole box exactly as before.
    auto project = [&](const double* c) noexcept -> Reason {
        if (!(c[3] > 0)) return Reason::NonPositiveW;
        const double sx = X + (c[0] / c[3] + 1.0) * half_w;
        const double sy = Y + (1.0 - c[1] / c[3]) * half_h;
        if (!std::isfinite(sx) || !std::isfinite(sy)) return Reason::NonFinite;
        if (first) {
            min_x = max_x = sx;
            min_y = max_y = sy;
            w_min = c[3];
            first = false;
        } else {
            w_min = c[3] < w_min ? c[3] : w_min;
            min_x = sx < min_x ? sx : min_x;
            max_x = sx > max_x ? sx : max_x;
            min_y = sy < min_y ? sy : min_y;
            max_y = sy > max_y ? sy : max_y;
        }
        return Reason::Bound;
    };
    for (unsigned corner = 0; corner < 8; ++corner) {
        if (clipping && clip[corner][2] < 0) continue;
        const Reason reason = project(clip[corner]);
        if (reason != Reason::Bound) return reason;
    }
    if (clipping && cut->clipped) {
        // The 12 edges: corners differing in exactly one axis bit. z is
        // affine along an edge, so the crossing is exact at t = za / (za - zb).
        for (unsigned a = 0; a < 8; ++a)
            for (unsigned axis = 0; axis < 3; ++axis) {
                const unsigned b = a ^ (1u << axis);
                if (b < a) continue;
                const double za = clip[a][2], zb = clip[b][2];
                if ((za < 0) == (zb < 0)) continue;
                const double t = za / (za - zb);
                double c[4];
                for (unsigned k = 0; k < 4; ++k) c[k] = clip[a][k] + t * (clip[b][k] - clip[a][k]);
                const Reason reason = project(c);
                if (reason != Reason::Bound) return reason;
            }
    }
    if (first) return Reason::NonFinite; // unreachable: at least one corner or crossing was projected
    // Bounded before the integer conversion: a huge box near w -> 0 projects
    // far outside any target; the viewport intersection below clamps it.
    constexpr double limit = 1e9;
    auto clamp = [](double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; };
    const std::int32_t pad = std::int32_t(pad_pixels(term_sum_max, w_min, half_w > half_h ? half_w : half_h));
    if (pad_out) *pad_out = unsigned(pad);
    const Rect hull{std::int32_t(scalar::floor(clamp(min_x, -limit, limit))) - pad,
                    std::int32_t(scalar::floor(clamp(min_y, -limit, limit))) - pad,
                    std::int32_t(scalar::ceil(clamp(max_x, -limit, limit))) + pad + 1,
                    std::int32_t(scalar::ceil(clamp(max_y, -limit, limit))) + pad + 1};
    Rect rect = intersect(hull, full_rect(viewport));
    if (empty(rect))
        rect = {std::int32_t(viewport.x), std::int32_t(viewport.y), std::int32_t(viewport.x) + 1,
                std::int32_t(viewport.y) + 1};
    *out = rect;
    return Reason::Bound;
}

// The rectangle a draw gets: a box-derived rectangle when the bound and the
// rows are known and the projection is well defined, else the full viewport;
// with the viewport itself unknown or empty, the caller's fallback (the whole
// owning target), so no consumer can ever compose nothing. rows == nullptr
// means unknown rows; bound_known false means no usable box.
struct Region {
    Rect rect{};
    Reason reason = Reason::Viewport;
    bool bound = false;   // rect came from the box (reason == Bound)
    unsigned clipped = 0; // corners cut away by the near plane (near_clip only; 0 otherwise)
    unsigned pad = 0;     // w-scaled fp32 pad actually applied, in pixels (Bound only)
};
inline Region derive(const float* rows, bool bound_known, const Box& box, const Viewport& viewport, bool fill_solid,
                     const Rect& fallback, bool near_clip = false) noexcept {
    Region region{};
    if (!viewport.width || !viewport.height) {
        region.reason = Reason::Viewport;
        region.rect = fallback;
        return region;
    }
    region.rect = full_rect(viewport);
    if (!rows) {
        region.reason = Reason::Rows;
        return region;
    }
    if (!fill_solid) {
        region.reason = Reason::FillMode;
        return region;
    }
    if (!bound_known) {
        region.reason = Reason::BoundUnknown;
        return region;
    }
    Rect rect{};
    NearClip cut{near_clip, 0};
    unsigned pad = 0;
    region.reason = project_box(rows, box, viewport, &rect, &cut, &pad);
    region.clipped = cut.clipped;
    region.pad = pad;
    if (region.reason == Reason::Bound) {
        region.rect = rect;
        region.bound = true;
    }
    return region;
}

// Step D (screen-emission-bullet-bound.md): the rectangle of a drawn
// TRIANGLELIST vertex prefix itself, from the exact positions the Unlock scan
// copied (3 floats per vertex, count a multiple of 3), through the same rows.
// Per triangle: the vertices with clip z >= 0 are projected; a triangle that
// straddles the near plane contributes its in-front vertices plus the exact
// crossings of the two edges that cross (z is affine along an edge, t =
// za / (za - zb)); a triangle entirely behind contributes nothing. D3D
// rasterises a pixel whose centre lies inside the clipped triangle, which
// lies inside the hull of those points, so the padded bounding rectangle of
// all of them contains every pixel any triangle of the prefix touches. The
// rectangle is the hull's bounding box: no polygon is built. A prefix with
// every triangle behind is BehindNear; a projected point with w <= 0 (rows
// that are not a perspective projection: under one, z >= 0 implies w >= zn >
// 0) is NonPositiveW; a nonfinite row, position or projection is NonFinite.
//
// The cut is made at z = -eps, eps = eps_dp4 * (largest z-row |term| sum over
// the prefix's extent), not at z = 0: the GPU evaluates the z row in fp32 too, so a
// vertex whose exact z lies within eps behind the plane may still be in
// front for the rasteriser, and a triangle hugging the plane could then be
// drawn whole while an exact cut kept only a sliver of it. Every point the
// hardware can rasterise has exact z >= -eps, so the polytope cut at -eps
// contains it (the term sum is convex along an edge, so the endpoint
// maximum bounds the crossing); w there is >= zn - eps > 0 under the game's
// rows. `clipped` counts the vertices behind that shifted plane.
//
// The pad is the w-scaled fp32 bound above with S the largest |term| sum
// over the x, y and w rows and the corners of the prefix's own extent (each
// sum is convex in the point, so the corner maximum bounds every vertex and
// crossing) and w_min over the projected points. No half-float expansion:
// the positions are the FLOAT3 values the GPU reads. Cost: one single-
// precision extent pass, 4 double dot products and one divide per vertex;
// no allocation. `info` receives the vertices cut away, the pad and the
// object-space extent of the prefix (the box the step-B route would have
// projected: the run-20 hull-versus-AABB comparison).
struct PrefixHull {
    unsigned behind = 0; // vertices with clip z < 0
    unsigned pad = 0;    // pad applied, in pixels (Bound only)
    Box aabb{};          // object-space extent of the prefix (finite positions only)
};
inline Reason project_prefix(const float rows[16], const float* positions, std::uint32_t count,
                             const Viewport& viewport, Rect* out, PrefixHull* info = nullptr) noexcept {
    if (!viewport.width || !viewport.height) return Reason::Viewport;
    for (unsigned i = 0; i < 16; ++i)
        if (!std::isfinite(rows[i])) return Reason::NonFinite;
    if (!positions || !count || count % 3) return Reason::BoundUnknown;
    // Pass 1: the prefix's extent (single precision, 3 compares per
    // component) and finiteness. The |term| sums the pad and the cut need
    // are convex in the point, so their maxima over the prefix are at the
    // corners of this box: 24 sums once per draw instead of four per vertex.
    float lo[3] = {positions[0], positions[1], positions[2]}, hi[3] = {positions[0], positions[1], positions[2]};
    for (std::uint32_t v = 0; v < count; ++v) {
        const float* q = positions + std::size_t(v) * 3;
        for (unsigned a = 0; a < 3; ++a) {
            const float x = q[a];
            if (!std::isfinite(x)) return Reason::NonFinite;
            lo[a] = x < lo[a] ? x : lo[a];
            hi[a] = x > hi[a] ? x : hi[a];
        }
    }
    double term_sum_max = 0, z_sum_max = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const double p[3] = {double((corner & 1u) ? hi[0] : lo[0]), double((corner & 2u) ? hi[1] : lo[1]),
                             double((corner & 4u) ? hi[2] : lo[2])};
        for (unsigned r = 0; r < 4; ++r) {
            const float* row = rows + 4 * r;
            const double sum = scalar::abs(double(row[0]) * p[0]) + scalar::abs(double(row[1]) * p[1]) +
                               scalar::abs(double(row[2]) * p[2]) + scalar::abs(double(row[3]));
            if (r == 2) {
                if (sum > z_sum_max) z_sum_max = sum;
            } // the z row decides the cut only
            else if (sum > term_sum_max)
                term_sum_max = sum;
        }
    }
    const double plane = -eps_dp4 * z_sum_max; // z >= plane: possibly rasterised
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0, w_min = 0;
    const double X = double(viewport.x), Y = double(viewport.y);
    const double half_w = double(viewport.width) * 0.5, half_h = double(viewport.height) * 0.5;
    bool first = true;
    unsigned behind = 0;
    auto project = [&](const double* c) noexcept -> Reason {
        if (!(c[3] > 0)) return Reason::NonPositiveW;
        const double sx = X + (c[0] / c[3] + 1.0) * half_w;
        const double sy = Y + (1.0 - c[1] / c[3]) * half_h;
        if (!std::isfinite(sx) || !std::isfinite(sy)) return Reason::NonFinite;
        if (first) {
            min_x = max_x = sx;
            min_y = max_y = sy;
            w_min = c[3];
            first = false;
        } else {
            w_min = c[3] < w_min ? c[3] : w_min;
            min_x = sx < min_x ? sx : min_x;
            max_x = sx > max_x ? sx : max_x;
            min_y = sy < min_y ? sy : min_y;
            max_y = sy > max_y ? sy : max_y;
        }
        return Reason::Bound;
    };
    // Pass 2: four double dot products per vertex (finite: finite rows and
    // positions cannot overflow a double), the cut, the projection.
    for (std::uint32_t v = 0; v < count; v += 3) {
        double clip[3][4];
        bool in_front[3];
        for (unsigned k = 0; k < 3; ++k) {
            const float* q = positions + std::size_t(v + k) * 3;
            const double p[3] = {double(q[0]), double(q[1]), double(q[2])};
            for (unsigned r = 0; r < 4; ++r) {
                const float* row = rows + 4 * r;
                clip[k][r] = double(row[0]) * p[0] + double(row[1]) * p[1] + double(row[2]) * p[2] + double(row[3]);
            }
            in_front[k] = !(clip[k][2] < plane);
            if (!in_front[k]) ++behind;
        }
        const unsigned front = unsigned(in_front[0]) + unsigned(in_front[1]) + unsigned(in_front[2]);
        if (!front) continue; // entirely behind the near plane: nothing rasterised
        for (unsigned k = 0; k < 3; ++k) {
            if (!in_front[k]) continue;
            const Reason reason = project(clip[k]);
            if (reason != Reason::Bound) return reason;
        }
        if (front == 3) continue;
        for (unsigned k = 0; k < 3; ++k) {
            const unsigned j = (k + 1) % 3;
            if (in_front[k] == in_front[j]) continue;
            const double za = clip[k][2] - plane, zb = clip[j][2] - plane;
            const double t = za / (za - zb);
            double c[4];
            for (unsigned r = 0; r < 4; ++r) c[r] = clip[k][r] + t * (clip[j][r] - clip[k][r]);
            const Reason reason = project(c);
            if (reason != Reason::Bound) return reason;
        }
    }
    if (info) {
        info->behind = behind;
        for (unsigned a = 0; a < 3; ++a) {
            info->aabb.centre[a] = (double(lo[a]) + double(hi[a])) * 0.5;
            info->aabb.half[a] = (double(hi[a]) - double(lo[a])) * 0.5;
        }
    }
    if (first) return Reason::BehindNear;
    constexpr double limit = 1e9;
    auto clamp = [](double v, double lo_, double hi_) { return v < lo_ ? lo_ : v > hi_ ? hi_ : v; };
    const std::int32_t pad = std::int32_t(pad_pixels(term_sum_max, w_min, half_w > half_h ? half_w : half_h));
    if (info) info->pad = unsigned(pad);
    const Rect hull{std::int32_t(scalar::floor(clamp(min_x, -limit, limit))) - pad,
                    std::int32_t(scalar::floor(clamp(min_y, -limit, limit))) - pad,
                    std::int32_t(scalar::ceil(clamp(max_x, -limit, limit))) + pad + 1,
                    std::int32_t(scalar::ceil(clamp(max_y, -limit, limit))) + pad + 1};
    Rect rect = intersect(hull, full_rect(viewport));
    if (empty(rect))
        rect = {std::int32_t(viewport.x), std::int32_t(viewport.y), std::int32_t(viewport.x) + 1,
                std::int32_t(viewport.y) + 1};
    *out = rect;
    return Reason::Bound;
}

// derive's twin for the locked-prefix source: the same precedence (viewport,
// rows, fill mode, then the positions), the prefix hull instead of the box.
// positions == nullptr or count 0 means no usable prefix (BoundUnknown).
inline Region derive_prefix(const float* rows, const float* positions, std::uint32_t count, const Viewport& viewport,
                            bool fill_solid, const Rect& fallback, PrefixHull* info = nullptr) noexcept {
    Region region{};
    if (!viewport.width || !viewport.height) {
        region.reason = Reason::Viewport;
        region.rect = fallback;
        return region;
    }
    region.rect = full_rect(viewport);
    if (!rows) {
        region.reason = Reason::Rows;
        return region;
    }
    if (!fill_solid) {
        region.reason = Reason::FillMode;
        return region;
    }
    if (!positions || !count) {
        region.reason = Reason::BoundUnknown;
        return region;
    }
    Rect rect{};
    PrefixHull hull{};
    region.reason = project_prefix(rows, positions, count, viewport, &rect, &hull);
    region.clipped = hull.behind;
    region.pad = hull.pad;
    if (info) *info = hull;
    if (region.reason == Reason::Bound) {
        region.rect = rect;
        region.bound = true;
    }
    return region;
}

} // namespace x3m::fade_region
