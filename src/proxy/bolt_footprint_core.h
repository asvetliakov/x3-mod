#pragma once
#include "sse_scalar.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Bolt footprint, option A' of docs/architecture/bolt-footprint.md: a minimum
// on-screen footprint for the bullet instances of an admitted screen-emission-
// additive draw, computed on the CPU from the vertices the locked-prefix scan
// copied at the buffer's DISCARD Unlock (locked_prefix_core.h: 3 position
// floats plus the 3 remaining words, UV and colour, per stride-24 vertex).
//
// Per draw: the instance period p is the smallest multiple of 3 in
// [min_period, max_period] dividing the drawn vertex count for which
// uv[i] == uv[i - p] (exact word equality) for every i >= p; the writer copies
// the same model UVs per instance (effects-engine-remaining-emission.md,
// "Bullet vertex buffer writer"), so a stream without such a period (per-object
// UV remap, a mixed record, a body longer than max_period) leaves the draw
// untouched. p == count is a period too (one instance in flight), which is why
// max_period is the largest stock body: a remapped stream of several bodies
// exceeds it and is refused; one of at most 234 vertices is indistinguishable
// from a single body and is treated as one instance. Three stock bodies repeat
// their own UVs (verification/results/bolt-footprint/bullet_uv_periods_out.txt):
// PlasmaBeam and Repair (72 = 3 x 24 crossed-card groups sharing the body's
// centroid and axis extent, so the split cannot change a verdict) and Repeat
// (234 = 3 x 78 collinear segments, which would); 78 is no stock body length,
// so a found period that is not a stock length is promoted to the smallest
// stock length it divides that also divides the count (78 -> 234). Per instance: the p vertices are projected through the c0-3 rows
// and the viewport to pixels; any clip w <= w_epsilon refuses the instance
// (behind or straddling the camera plane); centroid, 2x2 covariance, principal
// axes e1/e2 and half-extents A/B; t = saturate((A - R) / (G - R)), targets
// A* = R and B* = R (1 - t), scales s1 = max(1, A* / max(A, floor)) and
// s2 = max(1, B* / max(B, floor)); s1 = s2 = 1 (A >= G, or A >= R and B >= B*)
// leaves the instance's bytes exactly the game's. Otherwise each vertex gets
// the pixel displacement (s1 - 1)(d.e1) e1 + (s2 - 1)(d.e2) e2 about the
// centroid, converted to a world displacement in the plane perpendicular to
// the clip-w direction n = rows[12..14] (so clip w and clip z, both functions
// of view depth under a perspective row set, are unchanged and the depth test
// is the original's): alpha r + beta u with (r, u) an orthonormal basis of
// that plane and [a_r a_u] [alpha beta]^T = (dx w / half_w, -dy w / half_h).
// Rows whose z row is not parallel to the w row (not a perspective
// projection) refuse the draw: the depth invariance would not hold.
//
// Free of Windows and D3D; single-precision SSE only (no double, no
// <cmath> x87 paths: sse_scalar.h); nothing here allocates; every function is
// inline or returns void/bool/integers (a float returned from a non-inlined
// function would travel through st(0) on the i386 ABI). Host-tested by
// verification/analysis/test_bolt_footprint.py through a compiled harness.
namespace x3m::bolt_footprint {

constexpr unsigned stride = 24;
constexpr std::uint32_t min_period = 24;   // bullet_PointSing: 8 faces
constexpr std::uint32_t max_period = 234;  // bullet_Repeat: 78 faces = 234 vertices, the largest stock body; a longer
                                           // candidate is refused, so the vacuous period p == count admits at most one body
constexpr std::uint32_t max_vertices = 6144; // the scan bound (locked_prefix_core.h)
constexpr unsigned max_instances = max_vertices / min_period; // 256
constexpr float default_half_extent = 3.f; // R, px
constexpr float default_gate = 8.f;        // G, px: bolts with A >= G are never written
constexpr float w_epsilon = 1e-3f;         // clip w (view depth, metres) at or below: the instance is refused
constexpr float extent_floor = 0.05f;      // px: the scale never exceeds R / extent_floor
constexpr float parallel_tolerance = 1e-4f; // |z_row x w_row| / (|z_row| |w_row|) accepted as parallel
// Vertices per instance of the 20 stock bodies (bullet_bodies_out.txt), ascending.
constexpr std::uint32_t stock_body_lengths[] = {24, 36, 54, 66, 72, 84, 90, 108, 234};

#define X3M_BF_INLINE inline __attribute__((always_inline))

X3M_BF_INLINE float fabs_f(float x) noexcept {
    std::uint32_t bits; std::memcpy(&bits, &x, sizeof bits);
    bits &= 0x7fffffffu;
    std::memcpy(&x, &bits, sizeof x);
    return x;
}
X3M_BF_INLINE bool finite_f(float x) noexcept { return fabs_f(x) <= 3.4028235e38f; } // NaN fails the compare, +-inf exceeds the maximum
X3M_BF_INLINE float dot3(const float* a, const float* b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
X3M_BF_INLINE void cross3(const float* a, const float* b, float* out) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// R and G: finite, 0 < R <= 64, R < G <= 256 (pixels).
inline bool valid_parameters(float r, float g) noexcept {
    return finite_f(r) && finite_f(g) && r > 0.f && r <= 64.f && g > r && g <= 256.f;
}

// The instance period of a drawn prefix from its UV words (extras[i*3+0..1]),
// or 0 when none qualifies (fail closed). Cost: for each candidate divisor one
// pass until the first mismatch, so a wrong candidate usually fails at its
// first vertex.
inline std::uint32_t detect_period(const std::uint32_t* extras, std::uint32_t count) noexcept {
    if (!extras || count < min_period || count % 3u) return 0;
    for (std::uint32_t p = min_period; p <= max_period && p <= count; p += 3u) {
        if (count % p) continue;
        bool periodic = true;
        for (std::uint32_t i = p; i < count; ++i) {
            const std::uint32_t* a = extras + std::size_t(i) * 3u;
            const std::uint32_t* b = extras + std::size_t(i - p) * 3u;
            if (a[0] != b[0] || a[1] != b[1]) { periodic = false; break; }
        }
        if (periodic) {
            // A non-stock period inside a stock body: the segments of one body.
            for (const std::uint32_t body : stock_body_lengths) {
                if (body == p) return p;
                if (body > p && body % p == 0 && count % body == 0) return body;
            }
            return p;
        }
    }
    return 0;
}

enum class FrameReason : unsigned { Ok = 0, Viewport = 1, NonFinite = 2, Degenerate = 3, NotPerspective = 4, Count = 5 };
inline const char* frame_reason_name(FrameReason r) noexcept {
    static const char* const names[] = {"ok", "viewport", "nonfinite", "degenerate", "not_perspective"};
    const unsigned i = unsigned(r);
    return i < unsigned(FrameReason::Count) ? names[i] : "invalid";
}

// Per-draw constants: the rows, the viewport, the camera-plane basis and the
// inverse of the 2x2 pixel-to-basis map.
struct Frame {
    float rows[16]{};
    float x = 0, y = 0, half_w = 0, half_h = 0;
    float r[3]{}, u[3]{};   // orthonormal basis of the plane perpendicular to the clip-w direction
    float ar[2]{}, au[2]{}; // clip xy per unit of r and u
    float inv_det = 0;
    bool valid = false;
};

inline FrameReason prepare_frame(const float rows[16], std::uint32_t vx, std::uint32_t vy, std::uint32_t width, std::uint32_t height, Frame* out) noexcept {
    *out = Frame{};
    if (!rows || !width || !height) return FrameReason::Viewport;
    for (unsigned i = 0; i < 16; ++i) if (!finite_f(rows[i])) return FrameReason::NonFinite;
    std::memcpy(out->rows, rows, sizeof out->rows);
    out->x = float(vx); out->y = float(vy);
    out->half_w = float(width) * 0.5f; out->half_h = float(height) * 0.5f;
    const float* n = rows + 12; // the w row's world direction
    const float nn = dot3(n, n);
    if (!(nn > 0.f) || !finite_f(nn)) return FrameReason::Degenerate;
    // z row parallel to the w row: clip z and w both depend on view depth only.
    const float* z = rows + 8;
    const float zz = dot3(z, z);
    float c[3]; cross3(z, n, c);
    const float cc = dot3(c, c);
    if (!(cc <= parallel_tolerance * parallel_tolerance * zz * nn)) return FrameReason::NotPerspective;
    const float inv_len = 1.f / scalar::sqrt(nn);
    float nh[3] = {n[0] * inv_len, n[1] * inv_len, n[2] * inv_len};
    // The axis least aligned with n seeds the basis.
    float e[3] = {0.f, 0.f, 0.f};
    const float ax = fabs_f(nh[0]), ay = fabs_f(nh[1]), az = fabs_f(nh[2]);
    if (ax <= ay && ax <= az) e[0] = 1.f; else if (ay <= az) e[1] = 1.f; else e[2] = 1.f;
    float r[3]; cross3(nh, e, r);
    const float rr = dot3(r, r);
    if (!(rr > 0.f)) return FrameReason::Degenerate;
    const float inv_r = 1.f / scalar::sqrt(rr);
    for (unsigned i = 0; i < 3; ++i) out->r[i] = r[i] * inv_r;
    cross3(nh, out->r, out->u); // unit: nh and r are unit and perpendicular
    out->ar[0] = dot3(rows + 0, out->r); out->ar[1] = dot3(rows + 4, out->r);
    out->au[0] = dot3(rows + 0, out->u); out->au[1] = dot3(rows + 4, out->u);
    const float det = out->ar[0] * out->au[1] - out->ar[1] * out->au[0];
    if (!(fabs_f(det) > 1e-30f) || !finite_f(det)) return FrameReason::Degenerate;
    out->inv_det = 1.f / det;
    out->valid = true;
    return FrameReason::Ok;
}

// Projects one world position: pixel coordinates and clip w. false when
// w <= w_epsilon or the result is not finite.
X3M_BF_INLINE bool project_vertex(const Frame& f, const float* p, float* q, float* w_out) noexcept {
    const float cx = f.rows[0] * p[0] + f.rows[1] * p[1] + f.rows[2] * p[2] + f.rows[3];
    const float cy = f.rows[4] * p[0] + f.rows[5] * p[1] + f.rows[6] * p[2] + f.rows[7];
    const float w = f.rows[12] * p[0] + f.rows[13] * p[1] + f.rows[14] * p[2] + f.rows[15];
    if (!(w > w_epsilon) || !finite_f(w)) return false;
    const float inv_w = 1.f / w;
    q[0] = f.x + (cx * inv_w + 1.f) * f.half_w;
    q[1] = f.y + (1.f - cy * inv_w) * f.half_h;
    *w_out = w;
    return finite_f(q[0]) && finite_f(q[1]);
}

enum class Verdict : unsigned char { Untouched = 0, Expanded = 1, RefusedW = 2, NonFinite = 3 };

struct Plan {
    float qc[2]{};   // centroid, px
    float e1[2]{};   // major axis (e2 = (-e1.y, e1.x))
    float a = 0, b = 0; // half-extents along e1 / e2 before expansion, px
    float s1 = 1, s2 = 1;
    Verdict verdict = Verdict::Untouched;
};

// Analyses one instance of n <= max_period vertices (3 floats each).
inline void plan_instance(const Frame& f, const float* positions, std::uint32_t n, float r_px, float g_px, Plan* out) noexcept {
    *out = Plan{};
    if (!f.valid || !positions || !n || n > max_period) { out->verdict = Verdict::NonFinite; return; }
    float q[max_period][2];
    float sx = 0.f, sy = 0.f;
    for (std::uint32_t i = 0; i < n; ++i) {
        float w;
        if (!project_vertex(f, positions + std::size_t(i) * 3u, q[i], &w)) { out->verdict = Verdict::RefusedW; return; }
        sx += q[i][0]; sy += q[i][1];
    }
    const float inv_n = 1.f / float(n);
    const float cx = sx * inv_n, cy = sy * inv_n;
    out->qc[0] = cx; out->qc[1] = cy;
    float cxx = 0.f, cxy = 0.f, cyy = 0.f;
    for (std::uint32_t i = 0; i < n; ++i) {
        const float dx = q[i][0] - cx, dy = q[i][1] - cy;
        cxx += dx * dx; cxy += dx * dy; cyy += dy * dy;
    }
    // Major eigenvector of the symmetric 2x2 covariance.
    const float half_trace = (cxx + cyy) * 0.5f, half_diff = (cxx - cyy) * 0.5f;
    const float lambda = half_trace + scalar::sqrt(half_diff * half_diff + cxy * cxy);
    float e1[2] = {lambda - cyy, cxy};
    float ee = e1[0] * e1[0] + e1[1] * e1[1];
    if (!(ee > 1e-30f)) { e1[0] = cxy; e1[1] = lambda - cxx; ee = e1[0] * e1[0] + e1[1] * e1[1]; }
    if (!(ee > 1e-30f)) { e1[0] = 1.f; e1[1] = 0.f; ee = 1.f; }
    const float inv_e = 1.f / scalar::sqrt(ee);
    e1[0] *= inv_e; e1[1] *= inv_e;
    out->e1[0] = e1[0]; out->e1[1] = e1[1];
    float a = 0.f, b = 0.f;
    for (std::uint32_t i = 0; i < n; ++i) {
        const float dx = q[i][0] - cx, dy = q[i][1] - cy;
        const float pa = fabs_f(dx * e1[0] + dy * e1[1]), pb = fabs_f(-dx * e1[1] + dy * e1[0]);
        if (pa > a) a = pa;
        if (pb > b) b = pb;
    }
    out->a = a; out->b = b;
    if (!finite_f(a) || !finite_f(b)) { out->verdict = Verdict::NonFinite; return; }
    // The rule (bolt-footprint.md section 2, item 3).
    float t = (a - r_px) / (g_px - r_px);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    const float target_a = r_px, target_b = r_px * (1.f - t);
    const float base_a = a > extent_floor ? a : extent_floor, base_b = b > extent_floor ? b : extent_floor;
    float s1 = target_a / base_a, s2 = target_b / base_b;
    if (!(s1 > 1.f)) s1 = 1.f;
    if (!(s2 > 1.f)) s2 = 1.f;
    out->s1 = s1; out->s2 = s2;
    out->verdict = (s1 == 1.f && s2 == 1.f) ? Verdict::Untouched : Verdict::Expanded;
}

// Writes the n vertices of one instance to out (3 floats each) with the
// plan's displacement; an instance that is not Expanded is copied bit for
// bit. Returns false (and copies the instance unchanged) when a displaced
// position or its projection is not finite.
inline bool write_instance(const Frame& f, const float* positions, std::uint32_t n, const Plan& plan, float* out) noexcept {
    if (plan.verdict != Verdict::Expanded) { std::memcpy(out, positions, std::size_t(n) * 3u * sizeof(float)); return true; }
    const float k1 = plan.s1 - 1.f, k2 = plan.s2 - 1.f;
    const float e1x = plan.e1[0], e1y = plan.e1[1];
    for (std::uint32_t i = 0; i < n; ++i) {
        const float* p = positions + std::size_t(i) * 3u;
        float q[2], w;
        if (!project_vertex(f, p, q, &w)) { std::memcpy(out, positions, std::size_t(n) * 3u * sizeof(float)); return false; }
        const float dx = q[0] - plan.qc[0], dy = q[1] - plan.qc[1];
        const float pa = dx * e1x + dy * e1y, pb = -dx * e1y + dy * e1x;
        // Pixel displacement, then clip-space displacement at this vertex's w.
        const float ddx = k1 * pa * e1x - k2 * pb * e1y;
        const float ddy = k1 * pa * e1y + k2 * pb * e1x;
        const float ccx = ddx * w / f.half_w;
        const float ccy = -ddy * w / f.half_h;
        // [ar au] [alpha beta]^T = (ccx, ccy)
        const float alpha = (f.au[1] * ccx - f.au[0] * ccy) * f.inv_det;
        const float beta = (f.ar[0] * ccy - f.ar[1] * ccx) * f.inv_det;
        float* o = out + std::size_t(i) * 3u;
        for (unsigned k = 0; k < 3; ++k) {
            const float v = p[k] + alpha * f.r[k] + beta * f.u[k];
            if (!finite_f(v)) { std::memcpy(out, positions, std::size_t(n) * 3u * sizeof(float)); return false; }
            o[k] = v;
        }
    }
    return true;
}

struct DrawStats {
    std::uint32_t period = 0, instances = 0, expanded = 0, untouched = 0, refused_w = 0, nonfinite = 0;
};

// Plans every instance of a drawn prefix (count vertices). false: no period
// (stats->period == 0) or more instances than plans can hold; the draw stays
// untouched. true with stats->expanded == 0 means the draw needs no write.
inline bool plan_draw(const Frame& f, const float* positions, const std::uint32_t* extras, std::uint32_t count,
                      float r_px, float g_px, Plan* plans, unsigned plan_capacity, DrawStats* stats) noexcept {
    *stats = DrawStats{};
    if (!f.valid || !positions || !extras || !plans || !count || count > max_vertices) return false;
    const std::uint32_t p = detect_period(extras, count);
    if (!p) return false;
    const std::uint32_t instances = count / p;
    if (instances > plan_capacity) return false;
    stats->period = p; stats->instances = instances;
    for (std::uint32_t i = 0; i < instances; ++i) {
        plan_instance(f, positions + std::size_t(i) * p * 3u, p, r_px, g_px, plans + i);
        switch (plans[i].verdict) {
        case Verdict::Expanded: ++stats->expanded; break;
        case Verdict::Untouched: ++stats->untouched; break;
        case Verdict::RefusedW: ++stats->refused_w; break;
        default: ++stats->nonfinite; break;
        }
    }
    return true;
}

// Writes the whole prefix as stride-24 vertices into out (count * 24 bytes):
// the planned positions, then the vertex's own UV and colour words. Returns
// the instances actually expanded (a write_instance failure demotes one to a
// verbatim copy).
inline std::uint32_t write_draw(const Frame& f, const float* positions, const std::uint32_t* extras, std::uint32_t count,
                                std::uint32_t period, const Plan* plans, unsigned char* out) noexcept {
    if (!f.valid || !positions || !extras || !plans || !out || !period || count % period) return 0;
    const std::uint32_t instances = count / period;
    std::uint32_t expanded = 0;
    float scratch[max_period * 3];
    for (std::uint32_t i = 0; i < instances; ++i) {
        const float* src = positions + std::size_t(i) * period * 3u;
        const bool ok = write_instance(f, src, period, plans[i], scratch);
        if (ok && plans[i].verdict == Verdict::Expanded) ++expanded;
        for (std::uint32_t v = 0; v < period; ++v) {
            const std::size_t vertex = std::size_t(i) * period + v;
            unsigned char* o = out + vertex * stride;
            std::memcpy(o, scratch + std::size_t(v) * 3u, 12);
            std::memcpy(o + 12, extras + vertex * 3u, 12);
        }
    }
    return expanded;
}

#undef X3M_BF_INLINE
} // namespace x3m::bolt_footprint
