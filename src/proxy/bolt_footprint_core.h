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
// stock length it divides that also divides the count (78 -> 234).
//
// Per instance (the visibility rule, bolt-footprint.md "Run 73 B"): the p
// vertices are projected through the c0-3 rows and the viewport to pixels;
// any clip w <= w_epsilon refuses the instance (behind or straddling the
// camera plane). The length axis e_L on screen is the projection of the
// body's own world axis: the major eigenvector of the area-weighted second
// moment of its triangles about their area centroid (the surface's own
// covariance, which the triangle list's repeated diagonal vertices do not
// bias as a per-vertex covariance would) when the body is elongated (largest
// eigenvalue at least axis_elongation times the sum of the other two), taken
// as the derivative of the projection along it at that centroid, i.e. the streak's screen
// direction towards its vanishing point, which stays defined when the bolt is
// seen end-on (the chase view looks along the flight axis, where the 2D
// shape is only the cross-section); otherwise, or when that derivative
// vanishes, the major axis of the 2D pixel covariance when that shape is
// clearly elongated (extents ratio >= screen_elongation). With no usable axis
// (end-on exactly at the vanishing point, or a round body with a round
// shape) the footprint is a W x W square on the screen axes (Plan::disc: no
// lengthening; the 2D major axis would be rounding noise turning frame to
// frame). e_W = e_L rotated 90 degrees. Half-extents A (along e_L) and B (along e_W) about the pixel
// centroid; full length 2A below L_min scales along e_L by
// s1 = L_min / max(2A, 2 extent_floor), full width 2B below W_min scales
// along e_W by s2 = W_min / max(2B, 2 extent_floor), each 1 otherwise, so the
// rule is continuous in both extents except that a scale whose largest
// displacement stays below min_move_px is dropped (an axis the vertices do
// not span is never "expanded"), and s1 = s2 = 1 leaves the instance's bytes
// exactly the game's. Otherwise each vertex gets the pixel
// displacement (s1 - 1)(d.e_L) e_L + (s2 - 1)(d.e_W) e_W about the
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
constexpr std::uint32_t min_period = 24;  // bullet_PointSing: 8 faces
constexpr std::uint32_t max_period = 234; // bullet_Repeat: 78 faces = 234 vertices, the largest stock body; a longer
                                          // candidate is refused, so the vacuous period p == count admits at most one
                                          // body
constexpr std::uint32_t max_vertices = 6144;                  // the scan bound (locked_prefix_core.h)
constexpr unsigned max_instances = max_vertices / min_period; // 256
constexpr float default_min_width = 3.f;   // W_min, px: full projected width below it is widened to it
constexpr float default_min_length = 12.f; // L_min, px: full projected length below it is lengthened to it
constexpr float axis_elongation = 2.f;     // world axis used when lambda1 >= this x (lambda2 + lambda3)
constexpr unsigned axis_iterations = 16;   // power iterations for the world axis (converges as 0.5^k when elongated)
constexpr float axis_screen_min = 1e-3f;   // |d q / d s| below this x the perpendicular rate: no screen axis (end-on at
                                           // the vanishing point)
constexpr float screen_elongation = 1.5f;  // without a world axis: 2D extents ratio (sqrt of the covariance eigenvalue
                                           // ratio) below it -> the W x W disc
constexpr float min_move_px = 0.25f;  // a scale whose largest displacement stays below this is dropped (never a write
                                      // that moves nothing)
constexpr float w_epsilon = 1e-3f;    // clip w (view depth, metres) at or below: the instance is refused
constexpr float extent_floor = 0.05f; // px (half-extent): the scale never exceeds L_min / (2 extent_floor)
constexpr float parallel_tolerance = 1e-4f; // |z_row x w_row| / (|z_row| |w_row|) accepted as parallel
// Vertices per instance of the 20 stock bodies (bullet_bodies_out.txt), ascending.
constexpr std::uint32_t stock_body_lengths[] = {24, 36, 54, 66, 72, 84, 90, 108, 234};

#define X3M_BF_INLINE inline __attribute__((always_inline))

X3M_BF_INLINE float fabs_f(float x) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    bits &= 0x7fffffffu;
    std::memcpy(&x, &bits, sizeof x);
    return x;
}
X3M_BF_INLINE bool finite_f(float x) noexcept {
    return fabs_f(x) <= 3.4028235e38f;
} // NaN fails the compare, +-inf exceeds the maximum
X3M_BF_INLINE float dot3(const float* a, const float* b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
X3M_BF_INLINE void cross3(const float* a, const float* b, float* out) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// W_min and L_min: finite, 0 < W_min <= 64, W_min <= L_min <= 256 (pixels, full extents).
inline bool valid_parameters(float w_min, float l_min) noexcept {
    return finite_f(w_min) && finite_f(l_min) && w_min > 0.f && w_min <= 64.f && l_min >= w_min && l_min <= 256.f;
}

// Size histogram of the planned instances before any expansion (the
// bolt_footprint_hist row): half-length A and full width 2B in pixels, bucket
// i < hist_edge_count counts values below hist_edges[i] (and at or above the
// previous edge), the last bucket everything at or above 32 px.
constexpr float hist_edges[] = {0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f, 32.f};
constexpr unsigned hist_edge_count = sizeof hist_edges / sizeof hist_edges[0];
constexpr unsigned hist_buckets = hist_edge_count + 1;
struct Histogram {
    std::uint32_t instances = 0;
    std::uint32_t half_length[hist_buckets]{};
    std::uint32_t width[hist_buckets]{};
};
inline unsigned hist_bucket(float px) noexcept {
    unsigned i = 0;
    while (i < hist_edge_count && !(px < hist_edges[i])) ++i;
    return i;
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
            if (a[0] != b[0] || a[1] != b[1]) {
                periodic = false;
                break;
            }
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

enum class FrameReason : unsigned {
    Ok = 0,
    Viewport = 1,
    NonFinite = 2,
    Degenerate = 3,
    NotPerspective = 4,
    Count = 5
};
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

inline FrameReason prepare_frame(const float rows[16], std::uint32_t vx, std::uint32_t vy, std::uint32_t width,
                                 std::uint32_t height, Frame* out) noexcept {
    *out = Frame{};
    if (!rows || !width || !height) return FrameReason::Viewport;
    for (unsigned i = 0; i < 16; ++i)
        if (!finite_f(rows[i])) return FrameReason::NonFinite;
    std::memcpy(out->rows, rows, sizeof out->rows);
    out->x = float(vx);
    out->y = float(vy);
    out->half_w = float(width) * 0.5f;
    out->half_h = float(height) * 0.5f;
    const float* n = rows + 12; // the w row's world direction
    const float nn = dot3(n, n);
    if (!(nn > 0.f) || !finite_f(nn)) return FrameReason::Degenerate;
    // z row parallel to the w row: clip z and w both depend on view depth only.
    const float* z = rows + 8;
    const float zz = dot3(z, z);
    float c[3];
    cross3(z, n, c);
    const float cc = dot3(c, c);
    if (!(cc <= parallel_tolerance * parallel_tolerance * zz * nn)) return FrameReason::NotPerspective;
    const float inv_len = 1.f / scalar::sqrt(nn);
    float nh[3] = {n[0] * inv_len, n[1] * inv_len, n[2] * inv_len};
    // The axis least aligned with n seeds the basis.
    float e[3] = {0.f, 0.f, 0.f};
    const float ax = fabs_f(nh[0]), ay = fabs_f(nh[1]), az = fabs_f(nh[2]);
    if (ax <= ay && ax <= az)
        e[0] = 1.f;
    else if (ay <= az)
        e[1] = 1.f;
    else
        e[2] = 1.f;
    float r[3];
    cross3(nh, e, r);
    const float rr = dot3(r, r);
    if (!(rr > 0.f)) return FrameReason::Degenerate;
    const float inv_r = 1.f / scalar::sqrt(rr);
    for (unsigned i = 0; i < 3; ++i) out->r[i] = r[i] * inv_r;
    cross3(nh, out->r, out->u); // unit: nh and r are unit and perpendicular
    out->ar[0] = dot3(rows + 0, out->r);
    out->ar[1] = dot3(rows + 4, out->r);
    out->au[0] = dot3(rows + 0, out->u);
    out->au[1] = dot3(rows + 4, out->u);
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
    float qc[2]{};      // centroid, px
    float e1[2]{};      // length axis e_L (e_W = (-e1.y, e1.x))
    float a = 0, b = 0; // half-extents along e_L / e_W before expansion, px
    float s1 = 1, s2 = 1;
    Verdict verdict = Verdict::Untouched;
    bool world_axis = false; // e_L is the projected world axis (else the 2D covariance's major axis)
    bool disc = false;       // no usable axis: e_L = screen x, both extents raised to W only (a W x W footprint)
};

// The major eigenvector of a symmetric 3x3 (c = xx, xy, xz, yy, yz, zz) by
// power iteration from the column of the largest diagonal term; lambda1 is
// its Rayleigh quotient. false when the matrix is zero or the iteration
// leaves the finite range.
X3M_BF_INLINE bool major_axis3(const float* c, float* v, float* lambda1) noexcept {
    const float m[9] = {c[0], c[1], c[2], c[1], c[3], c[4], c[2], c[4], c[5]};
    unsigned k = 0;
    if (c[3] > c[0]) k = 1;
    if (c[5] > m[k * 4]) k = 2;
    float x[3] = {m[k], m[3 + k], m[6 + k]};
    for (unsigned it = 0; it < axis_iterations; ++it) {
        const float y[3] = {dot3(m, x), dot3(m + 3, x), dot3(m + 6, x)};
        const float yy = dot3(y, y);
        if (!(yy > 1e-30f) || !finite_f(yy)) return false;
        const float inv = 1.f / scalar::sqrt(yy);
        x[0] = y[0] * inv;
        x[1] = y[1] * inv;
        x[2] = y[2] * inv;
    }
    const float mx[3] = {dot3(m, x), dot3(m + 3, x), dot3(m + 6, x)};
    *lambda1 = dot3(x, mx);
    v[0] = x[0];
    v[1] = x[1];
    v[2] = x[2];
    return finite_f(*lambda1);
}

// Analyses one instance of n <= max_period vertices (3 floats each) against
// the minimum full width w_px and full length l_px.
inline void plan_instance(const Frame& f, const float* positions, std::uint32_t n, float w_px, float l_px,
                          Plan* out) noexcept {
    *out = Plan{};
    if (!f.valid || !positions || !n || n > max_period) {
        out->verdict = Verdict::NonFinite;
        return;
    }
    float q[max_period][2];
    float sx = 0.f, sy = 0.f;
    for (std::uint32_t i = 0; i < n; ++i) {
        float w;
        if (!project_vertex(f, positions + std::size_t(i) * 3u, q[i], &w)) {
            out->verdict = Verdict::RefusedW;
            return;
        }
        sx += q[i][0];
        sy += q[i][1];
    }
    const float inv_n = 1.f / float(n);
    const float cx = sx * inv_n, cy = sy * inv_n;
    out->qc[0] = cx;
    out->qc[1] = cy;
    // The 2D shape's covariance: area-weighted over the projected triangles
    // (the repeated diagonal vertices of a triangle list bias a per-vertex
    // covariance, 20 degrees on a round crossed-card body in the host oracle);
    // the per-vertex one only when every triangle is edge-on on screen.
    float cxx = 0.f, cxy = 0.f, cyy = 0.f;
    {
        float sa = 0.f, sm[2] = {0.f, 0.f}, sxx = 0.f, sxy = 0.f, syy = 0.f;
        for (std::uint32_t t = 0; t + 2u < n; t += 3u) {
            const float x0 = q[t][0] - cx, y0 = q[t][1] - cy, x1 = q[t + 1][0] - cx, y1 = q[t + 1][1] - cy,
                        x2 = q[t + 2][0] - cx, y2 = q[t + 2][1] - cy;
            const float at = 0.5f * fabs_f((x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0));
            if (!(at > 0.f) || !finite_f(at)) continue;
            const float gx = x0 + x1 + x2, gy = y0 + y1 + y2, k = at * (1.f / 12.f);
            sa += at;
            sm[0] += at * (1.f / 3.f) * gx;
            sm[1] += at * (1.f / 3.f) * gy;
            sxx += k * (x0 * x0 + x1 * x1 + x2 * x2 + gx * gx);
            sxy += k * (x0 * y0 + x1 * y1 + x2 * y2 + gx * gy);
            syy += k * (y0 * y0 + y1 * y1 + y2 * y2 + gy * gy);
        }
        if (sa > 0.f && finite_f(sa)) {
            const float inv = 1.f / sa, mx = sm[0] * inv, my = sm[1] * inv;
            cxx = sxx * inv - mx * mx;
            cxy = sxy * inv - mx * my;
            cyy = syy * inv - my * my;
        } else {
            for (std::uint32_t i = 0; i < n; ++i) {
                const float dx = q[i][0] - cx, dy = q[i][1] - cy;
                cxx += dx * dx;
                cxy += dx * dy;
                cyy += dy * dy;
            }
        }
    }
    // The surface's second moment in world space, per triangle (n is a
    // multiple of 3: a triangle list): area A, first moment A (a+b+c)/3 and
    // second moment A/12 (aa' + bb' + cc' + ss'), s = a+b+c, on offsets from
    // the first vertex (|p| ~ 1e5 in a sector: the offsets keep the body's
    // metres in float precision).
    const float* p0 = positions;
    float area = 0.f, m1[3] = {0.f, 0.f, 0.f}, m2[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    for (std::uint32_t t = 0; t + 2u < n; t += 3u) {
        float v[3][3];
        for (unsigned k = 0; k < 3; ++k) {
            const float* p = positions + std::size_t(t + k) * 3u;
            v[k][0] = p[0] - p0[0];
            v[k][1] = p[1] - p0[1];
            v[k][2] = p[2] - p0[2];
        }
        const float e0[3] = {v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]};
        const float e2[3] = {v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2]};
        float nrm[3];
        cross3(e0, e2, nrm);
        const float at = 0.5f * scalar::sqrt(dot3(nrm, nrm));
        if (!(at > 0.f) || !finite_f(at)) continue;
        const float sum[3] = {v[0][0] + v[1][0] + v[2][0], v[0][1] + v[1][1] + v[2][1], v[0][2] + v[1][2] + v[2][2]};
        area += at;
        const float third = at * (1.f / 3.f), twelfth = at * (1.f / 12.f);
        m1[0] += third * sum[0];
        m1[1] += third * sum[1];
        m1[2] += third * sum[2];
        const unsigned rr[6] = {0, 0, 0, 1, 1, 2}, cc[6] = {0, 1, 2, 1, 2, 2};
        for (unsigned j = 0; j < 6; ++j) {
            const unsigned r = rr[j], c = cc[j];
            m2[j] += twelfth * (v[0][r] * v[0][c] + v[1][r] * v[1][c] + v[2][r] * v[2][c] + sum[r] * sum[c]);
        }
    }
    // Fallback axis: the major eigenvector of the symmetric 2x2 shape covariance.
    const float half_trace = (cxx + cyy) * 0.5f, half_diff = (cxx - cyy) * 0.5f;
    const float lambda = half_trace + scalar::sqrt(half_diff * half_diff + cxy * cxy);
    float e1[2] = {lambda - cyy, cxy};
    float ee = e1[0] * e1[0] + e1[1] * e1[1];
    if (!(ee > 1e-30f)) {
        e1[0] = cxy;
        e1[1] = lambda - cxx;
        ee = e1[0] * e1[0] + e1[1] * e1[1];
    }
    if (!(ee > 1e-30f)) {
        e1[0] = 1.f;
        e1[1] = 0.f;
        ee = 1.f;
    }
    float inv_e = 1.f / scalar::sqrt(ee);
    e1[0] *= inv_e;
    e1[1] *= inv_e;
    // Screen elongation of the 2D shape (eigenvalue ratio of its covariance).
    const float lambda_minor = (cxx + cyy) - lambda;
    const bool screen_elongated = lambda > 0.f && lambda >= screen_elongation * screen_elongation *
                                                                (lambda_minor > 0.f ? lambda_minor : 0.f);
    // The world axis of an elongated body, projected at the area centroid:
    // d(pixel)/ds along it = (half_w (cx' w - cx w'), -half_h (cy' w - cy w')) / w^2.
    float axis[3], lambda1 = 0.f, c3[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f}, mu[3] = {0.f, 0.f, 0.f};
    if (area > 0.f && finite_f(area)) {
        const float inv_a = 1.f / area;
        mu[0] = m1[0] * inv_a;
        mu[1] = m1[1] * inv_a;
        mu[2] = m1[2] * inv_a;
        c3[0] = m2[0] * inv_a - mu[0] * mu[0];
        c3[1] = m2[1] * inv_a - mu[0] * mu[1];
        c3[2] = m2[2] * inv_a - mu[0] * mu[2];
        c3[3] = m2[3] * inv_a - mu[1] * mu[1];
        c3[4] = m2[4] * inv_a - mu[1] * mu[2];
        c3[5] = m2[5] * inv_a - mu[2] * mu[2];
    }
    const float trace3 = c3[0] + c3[3] + c3[5];
    bool world_elongated = false;
    if (trace3 > 0.f && finite_f(trace3) && major_axis3(c3, axis, &lambda1) &&
        lambda1 >= axis_elongation * (trace3 - lambda1)) {
        world_elongated = true;
        const float P[3] = {p0[0] + mu[0], p0[1] + mu[1], p0[2] + mu[2]};
        const float* r = f.rows;
        const float ccx = dot3(r, P) + r[3], ccy = dot3(r + 4, P) + r[7], cw = dot3(r + 12, P) + r[15];
        const float dcx = dot3(r, axis), dcy = dot3(r + 4, axis), dcw = dot3(r + 12, axis);
        if (cw > w_epsilon) {
            // Both components times w^2; the threshold is axis_screen_min of the
            // rate of an axis perpendicular to the view, half_w |row0| / w, times w^2.
            const float gx = f.half_w * (dcx * cw - ccx * dcw), gy = -f.half_h * (dcy * cw - ccy * dcw);
            const float gg = gx * gx + gy * gy;
            const float rate = axis_screen_min * f.half_w * cw * scalar::sqrt(dot3(r, r));
            if (finite_f(gg) && gg > rate * rate && gg > 1e-30f) {
                inv_e = 1.f / scalar::sqrt(gg);
                e1[0] = gx * inv_e;
                e1[1] = gy * inv_e;
                out->world_axis = true;
            }
        }
    }
    // No usable axis: an elongated body whose projected axis vanishes (seen
    // exactly end-on at its vanishing point), or a round body whose 2D shape
    // is not clearly elongated. The 2D major axis is rounding noise there and
    // would turn frame to frame, so the footprint is a W x W square on the
    // screen axes: no lengthening, a stable, direction-free result.
    if (!out->world_axis && (world_elongated || !screen_elongated)) {
        out->disc = true;
        e1[0] = 1.f;
        e1[1] = 0.f;
        l_px = w_px;
    }
    out->e1[0] = e1[0];
    out->e1[1] = e1[1];
    float a = 0.f, b = 0.f;
    for (std::uint32_t i = 0; i < n; ++i) {
        const float dx = q[i][0] - cx, dy = q[i][1] - cy;
        const float pa = fabs_f(dx * e1[0] + dy * e1[1]), pb = fabs_f(-dx * e1[1] + dy * e1[0]);
        if (pa > a) a = pa;
        if (pb > b) b = pb;
    }
    out->a = a;
    out->b = b;
    if (!finite_f(a) || !finite_f(b)) {
        out->verdict = Verdict::NonFinite;
        return;
    }
    // The rule (bolt-footprint.md, "Run 73 B"): full extents against the minimums.
    const float full_floor = 2.f * extent_floor;
    const float length = 2.f * a, width = 2.f * b;
    float s1 = length < l_px ? l_px / (length > full_floor ? length : full_floor) : 1.f;
    float s2 = width < w_px ? w_px / (width > full_floor ? width : full_floor) : 1.f;
    if (!(s1 > 1.f)) s1 = 1.f;
    if (!(s2 > 1.f)) s2 = 1.f;
    // An axis the vertices do not span (zero extent, or so small that the
    // scale moves no vertex by min_move_px) is left alone: a write must move
    // something, so an instance whose both scales drop is Untouched.
    if ((s1 - 1.f) * a < min_move_px) s1 = 1.f;
    if ((s2 - 1.f) * b < min_move_px) s2 = 1.f;
    out->s1 = s1;
    out->s2 = s2;
    out->verdict = (s1 == 1.f && s2 == 1.f) ? Verdict::Untouched : Verdict::Expanded;
}

// Writes the n vertices of one instance to out (3 floats each) with the
// plan's displacement; an instance that is not Expanded is copied bit for
// bit. Returns false (and copies the instance unchanged) when a displaced
// position or its projection is not finite.
inline bool write_instance(const Frame& f, const float* positions, std::uint32_t n, const Plan& plan,
                           float* out) noexcept {
    if (plan.verdict != Verdict::Expanded) {
        std::memcpy(out, positions, std::size_t(n) * 3u * sizeof(float));
        return true;
    }
    const float k1 = plan.s1 - 1.f, k2 = plan.s2 - 1.f;
    const float e1x = plan.e1[0], e1y = plan.e1[1];
    for (std::uint32_t i = 0; i < n; ++i) {
        const float* p = positions + std::size_t(i) * 3u;
        float q[2], w;
        if (!project_vertex(f, p, q, &w)) {
            std::memcpy(out, positions, std::size_t(n) * 3u * sizeof(float));
            return false;
        }
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
            if (!finite_f(v)) {
                std::memcpy(out, positions, std::size_t(n) * 3u * sizeof(float));
                return false;
            }
            o[k] = v;
        }
    }
    return true;
}

struct DrawStats {
    std::uint32_t period = 0, instances = 0, expanded = 0, untouched = 0, refused_w = 0, nonfinite = 0;
    std::uint32_t lengthened = 0, widened = 0, world_axis = 0, disc = 0; // expanded along e_L / e_W; planned on the
                                                                         // world axis; W x W disc
};

// Plans every instance of a drawn prefix (count vertices). false: no period
// (stats->period == 0) or more instances than plans can hold; the draw stays
// untouched. true with stats->expanded == 0 means the draw needs no write.
// hist (optional) receives every projected instance's half-length and width
// before expansion.
inline bool plan_draw(const Frame& f, const float* positions, const std::uint32_t* extras, std::uint32_t count,
                      float w_px, float l_px, Plan* plans, unsigned plan_capacity, DrawStats* stats,
                      Histogram* hist = nullptr) noexcept {
    *stats = DrawStats{};
    if (!f.valid || !positions || !extras || !plans || !count || count > max_vertices) return false;
    const std::uint32_t p = detect_period(extras, count);
    if (!p) return false;
    const std::uint32_t instances = count / p;
    if (instances > plan_capacity) return false;
    stats->period = p;
    stats->instances = instances;
    for (std::uint32_t i = 0; i < instances; ++i) {
        Plan& plan = plans[i];
        plan_instance(f, positions + std::size_t(i) * p * 3u, p, w_px, l_px, &plan);
        switch (plan.verdict) {
        case Verdict::Expanded:
            ++stats->expanded;
            stats->lengthened += plan.s1 > 1.f;
            stats->widened += plan.s2 > 1.f;
            break;
        case Verdict::Untouched: ++stats->untouched; break;
        case Verdict::RefusedW: ++stats->refused_w; break;
        default: ++stats->nonfinite; break;
        }
        if (plan.verdict == Verdict::Expanded || plan.verdict == Verdict::Untouched) {
            stats->world_axis += plan.world_axis;
            stats->disc += plan.disc;
            if (hist) {
                ++hist->instances;
                ++hist->half_length[hist_bucket(plan.a)];
                ++hist->width[hist_bucket(2.f * plan.b)];
            }
        }
    }
    return true;
}

// The cheap size histogram of a drawn prefix for views that are never written
// (bolt_footprint_hist view=other): per instance the projected bounding box,
// half-length = half its larger side, width = its smaller side; no moments,
// no axis. false when no period qualifies. Instances with a vertex at or
// behind w_epsilon are skipped.
inline bool histogram_draw(const Frame& f, const float* positions, const std::uint32_t* extras, std::uint32_t count,
                           Histogram* hist) noexcept {
    if (!f.valid || !positions || !extras || !hist || !count || count > max_vertices) return false;
    const std::uint32_t p = detect_period(extras, count);
    if (!p) return false;
    for (std::uint32_t i = 0; i < count / p; ++i) {
        float lo[2] = {3.4e38f, 3.4e38f}, hi[2] = {-3.4e38f, -3.4e38f};
        bool ok = true;
        for (std::uint32_t v = 0; v < p && ok; ++v) {
            float q[2], w;
            ok = project_vertex(f, positions + (std::size_t(i) * p + v) * 3u, q, &w);
            if (!ok) break;
            for (unsigned k = 0; k < 2; ++k) {
                if (q[k] < lo[k]) lo[k] = q[k];
                if (q[k] > hi[k]) hi[k] = q[k];
            }
        }
        if (!ok) continue;
        const float sx = hi[0] - lo[0], sy = hi[1] - lo[1];
        const float major = sx > sy ? sx : sy, minor = sx > sy ? sy : sx;
        ++hist->instances;
        ++hist->half_length[hist_bucket(0.5f * major)];
        ++hist->width[hist_bucket(minor)];
    }
    return true;
}

// Writes the whole prefix as stride-24 vertices into out (count * 24 bytes):
// the planned positions, then the vertex's own UV and colour words. Returns
// the instances actually expanded (a write_instance failure demotes one to a
// verbatim copy).
inline std::uint32_t write_draw(const Frame& f, const float* positions, const std::uint32_t* extras,
                                std::uint32_t count, std::uint32_t period, const Plan* plans,
                                unsigned char* out) noexcept {
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
