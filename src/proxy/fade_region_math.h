#pragma once
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

// MotionOutput::apply_jitter's arithmetic, bit for bit (single precision,
// SSE): rows[0] += jx_ndc * rows[3], rows[1] += jy_ndc * rows[3].
inline void jitter_rows(float rows[16], float jx_px, float jy_px, unsigned width, unsigned height) noexcept {
    const float jx = 2.f * jx_px / float(width), jy = -2.f * jy_px / float(height);
    for (unsigned k = 0; k < 4; ++k) { rows[k] += jx * rows[12 + k]; rows[4 + k] += jy * rows[12 + k]; }
}

inline Rect full_rect(const Viewport& v) noexcept {
    return {std::int32_t(v.x), std::int32_t(v.y), std::int32_t(v.x + v.width), std::int32_t(v.y + v.height)};
}
inline bool empty(const Rect& r) noexcept { return r.right <= r.left || r.bottom <= r.top; }
inline Rect intersect(const Rect& a, const Rect& b) noexcept {
    Rect r{a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
           a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom};
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
// rectangle [floor(min) - 1, ceil(max) + 1] inclusive on both axes,
// intersected with the viewport. An empty intersection yields the 1x1
// rectangle at the viewport origin. Returns Reason::Bound and writes *out;
// any other reason leaves *out untouched and the caller uses the full viewport.
inline Reason project_box(const float rows[16], const Box& box, const Viewport& viewport, Rect* out, NearClip* cut = nullptr) noexcept {
    if (!viewport.width || !viewport.height) return Reason::Viewport;
    for (unsigned i = 0; i < 16; ++i) if (!std::isfinite(rows[i])) return Reason::NonFinite;
    for (unsigned a = 0; a < 3; ++a) {
        if (!std::isfinite(box.centre[a]) || !std::isfinite(box.half[a])) return Reason::NonFinite;
        if (!(box.half[a] >= 0)) return Reason::BoundUnknown;
    }
    double clip[8][4];
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
        }
        // Unclipped: refuse at the first corner with w <= 0, before the later
        // corners are evaluated (the fade route's reason histogram unchanged).
        if (!(cut && cut->enabled) && !(clip[corner][3] > 0)) return Reason::NonPositiveW;
    }
    const bool clipping = cut && cut->enabled;
    if (cut) cut->clipped = 0;
    if (clipping) {
        unsigned behind = 0;
        for (unsigned corner = 0; corner < 8; ++corner) if (clip[corner][2] < 0) ++behind;
        cut->clipped = behind;
        if (behind == 8) return Reason::BehindNear;
    }
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
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
        if (first) { min_x = max_x = sx; min_y = max_y = sy; first = false; }
        else {
            min_x = sx < min_x ? sx : min_x; max_x = sx > max_x ? sx : max_x;
            min_y = sy < min_y ? sy : min_y; max_y = sy > max_y ? sy : max_y;
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
        for (unsigned a = 0; a < 8; ++a) for (unsigned axis = 0; axis < 3; ++axis) {
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
    const Rect hull{std::int32_t(std::floor(clamp(min_x, -limit, limit))) - 1,
                    std::int32_t(std::floor(clamp(min_y, -limit, limit))) - 1,
                    std::int32_t(std::ceil(clamp(max_x, -limit, limit))) + 2,
                    std::int32_t(std::ceil(clamp(max_y, -limit, limit))) + 2};
    Rect rect = intersect(hull, full_rect(viewport));
    if (empty(rect)) rect = {std::int32_t(viewport.x), std::int32_t(viewport.y), std::int32_t(viewport.x) + 1, std::int32_t(viewport.y) + 1};
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
};
inline Region derive(const float* rows, bool bound_known, const Box& box, const Viewport& viewport, bool fill_solid, const Rect& fallback, bool near_clip = false) noexcept {
    Region region{};
    if (!viewport.width || !viewport.height) { region.reason = Reason::Viewport; region.rect = fallback; return region; }
    region.rect = full_rect(viewport);
    if (!rows) { region.reason = Reason::Rows; return region; }
    if (!fill_solid) { region.reason = Reason::FillMode; return region; }
    if (!bound_known) { region.reason = Reason::BoundUnknown; return region; }
    Rect rect{};
    NearClip cut{near_clip, 0};
    region.reason = project_box(rows, box, viewport, &rect, &cut);
    region.clipped = cut.clipped;
    if (region.reason == Reason::Bound) { region.rect = rect; region.bound = true; }
    return region;
}

} // namespace x3m::fade_region
