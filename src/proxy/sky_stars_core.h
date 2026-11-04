#pragma once
// Sky stars, Decision A (docs/architecture/sector-background-modern.md, "Implementation A"): the pure part of the runtime
// procedural star pass. The painted star texture of a sector background carries the baker's X3ML mark with payload 2
// (tools/analysis/sky_textures.py: slot = fnv1a32(family) & 0xff, lane = colour class, K = bright peaks / 8); the route
// masks the star body's colour writes and draws one additive batch of star quads in its place, seeded from (slot, lane,
// K) alone. This header holds the seed, the star count, the static vertex buffer's contents and the kernel the pixel
// program evaluates. No Windows/D3D dependency, no allocation, no CRT math, no float returned by value from an
// out-of-line function (an i386 float return is an x87 st0 load; check_no_x87.py walks the draw path that builds the
// buffer): verification/analysis/test_sky_stars.py compiles exactly this header (verification/probe/sky_stars_check.cpp).
//
// Brightness law (target values, the FP16 scene's engine space; the installed hdr_decode gamma 2.2 maps 1.0 to 1.0):
//   decoded peak p = 1 / X, X uniform in [1, 2.5^4]   (count per 2.5x flux step rises 2.5x: the power law; the step is
//                                                        2.5, not Pogson's 2.512, so p needs no CRT pow)
//   target peak  = p^(1/2.2)                          (brightest 1.0, median decoded 2 / (1 + 39.06) = 0.0499)
//   energy       = target peak / centre_fraction(a)   (the sum over the covered pixels, any sub-pixel position)
// Kernel: separable Beta(3,3) density over [-a, a] px per axis (Gaussian-shaped, sigma = 0.378 a), whose CDF is the
// quintic smootherstep C(t) = t^3 (t (6 t - 15) + 10), t = (x + a) / (2 a). A pixel at offset u (pixel centre minus star
// centre) receives (C(t(u + 0.5)) - C(t(u - 0.5))) per axis; the sum over every pixel telescopes to C(1) - C(0) = 1
// exactly when every pixel whose area meets [-a, a] is rasterised, which the quad's half extent a + quad_margin
// guarantees. a = max(1 px, the smallest support whose centred peak stays <= 1.0): the footprint is max(2 px, s(m)).
#include "lod_texture_mark.h"
#include "sse_scalar.h"
#include <cstdint>

namespace x3m::sky_stars::core {
constexpr unsigned payload_sky_stars = lod_texture_mark::payload_sky_stars; // descriptor bits 24-31 of a star texture
constexpr unsigned lane_count = 8;            // sky_textures.STAR_LANES: white blue red orange yellow green cyan, 7 other
constexpr unsigned stars_per_k = 48, stars_min = 512, stars_max = 8192;
constexpr float density_default = 1.f, density_min = .25f, density_max = 4.f; // sky_stars_density
constexpr unsigned vertex_stride = 28, vertices_per_star = 4, indices_per_star = 6;
constexpr unsigned vertex_capacity = stars_max * vertices_per_star; // 32768: 16-bit indices suffice
constexpr float flux_step = 2.5f, flux_steps = 4.f, x_max = 39.0625f; // 2.5^4
constexpr double decode_gamma = 2.2;                                    // the installed hdr_decode (gamma2.2)
constexpr float half_support_min = 1.f; // px: the 2 px minimum footprint
constexpr float half_support_max = 8.f; // px: the cap the support search stops at (never reached by the law above)
constexpr float quad_margin = .5625f;   // px beyond the support: every pixel with weight has its centre inside the quad
// The packed seed the sampler shadow carries for stage 0: slot | lane << 8 | K << 16 | valid << 24; 0 = not a star texture.
constexpr std::uint32_t seed_valid = 0x01000000u;
inline std::uint32_t seed_of(const lod_texture_mark::Mark& m) noexcept {
    if (m.version != lod_texture_mark::version || m.payload != payload_sky_stars || m.slot > 255u || m.lane > 255u ||
        m.k > 255u)
        return 0;
    return m.slot | m.lane << 8 | m.k << 16 | seed_valid;
}
inline unsigned seed_slot(std::uint32_t seed) noexcept { return seed & 255u; }
inline unsigned seed_lane(std::uint32_t seed) noexcept { return (seed >> 8) & 255u; }
inline unsigned seed_k(std::uint32_t seed) noexcept { return (seed >> 16) & 255u; }
// The accepted density (sky_stars_density): a finite value in [density_min, density_max] replaces *out.
inline bool accept_density(float v, float* out) noexcept {
    if (!(v >= density_min && v <= density_max)) return false;
    *out = v;
    return true;
}
// N = clamp(round(K x 48 x density), 512, 8192).
inline unsigned star_count(unsigned k, float density) noexcept {
    if (!(density >= density_min && density <= density_max)) density = density_default;
    const float n = float(k) * float(stars_per_k) * density + .5f;
    if (!(n >= float(stars_min))) return stars_min;
    if (n >= float(stars_max)) return stars_max;
    return unsigned(n);
}
struct Vertex {
    float dir[3];        // unit direction in world orientation (stars sit at infinity: rotation only)
    float corner[2];     // quad corner offset in px, (+-h, +-h), h = a + quad_margin
    float energy;        // the summed target value of the star's brightest channel
    std::uint32_t tint;  // D3DCOLOR (A8R8G8B8), the brightest channel 255
};
static_assert(sizeof(Vertex) == vertex_stride, "the declaration follows the vertex");

// ---- Kernel (the pixel program's arithmetic) ----
[[gnu::always_inline]] inline double smootherstep(double t) noexcept {
    t = t < 0. ? 0. : t > 1. ? 1. : t;
    return t * t * t * (t * (t * 6. - 15.) + 10.);
}
// The weight of the pixel whose centre sits u px from the star centre along one axis, support half-width a.
[[gnu::always_inline]] inline double axis_weight(double u, double a) noexcept {
    const double s = .5 / a;
    return smootherstep((u + .5) * s + .5) - smootherstep((u - .5) * s + .5);
}
// The largest pixel weight of a support a (a pixel centred on the star), both axes.
[[gnu::always_inline]] inline double centre_fraction(double a) noexcept {
    const double w = axis_weight(0., a);
    return w * w;
}
// The support half-width of a star with target peak `peak` (<= 1 by the law; the search keeps the cap a hard rule for
// any caller): the smallest a >= half_support_min in 1/16 px steps with peak / centre_fraction(1) * centre_fraction(a)
// <= 1, i.e. the energy fixed at the minimum footprint's and the footprint grown until the centred peak is under 1.0.
inline void half_support(double energy, double* a_out) noexcept {
    double a = half_support_min;
    while (a < half_support_max && energy * centre_fraction(a) > 1. + 1e-9)
        a += 1. / 16.;
    *a_out = a;
}

// ---- Deterministic generator (no CRT) ----
struct Rng {
    std::uint32_t state;
    explicit Rng(std::uint32_t seed) noexcept {
        // murmur3 finaliser: nearby seeds give unrelated streams; 0 is avoided (xorshift's fixed point).
        std::uint32_t h = seed ^ 0x9e3779b9u;
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        state = h ? h : 0x6d2b79f5u;
    }
    std::uint32_t next() noexcept {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state = x;
    }
    // [0, 1) in 2^-24 steps, by integer conversion only.
    void uniform(double* out) noexcept { *out = double(next() >> 8) * (1. / 16777216.); }
};
// log2 and exp2 without the CRT (doubles, SSE2 in the DLL): |error| < 1e-9 over the generator's range.
[[gnu::always_inline]] inline double log2_positive(double x) noexcept {
    // x = m * 2^e, m in [1, 2), by the IEEE fields; ln m by the atanh series in s = (m - 1) / (m + 1) <= 1/3.
    std::uint64_t bits;
    __builtin_memcpy(&bits, &x, sizeof bits);
    const int e = int((bits >> 52) & 0x7ff) - 1023;
    bits = (bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
    double m;
    __builtin_memcpy(&m, &bits, sizeof m);
    const double s = (m - 1.) / (m + 1.), s2 = s * s;
    double sum = 0., term = s;
    for (unsigned k = 1; k <= 23; k += 2) {
        sum += term / double(k);
        term *= s2;
    }
    return double(e) + 2. * sum * 1.4426950408889634;
}
[[gnu::always_inline]] inline double exp2_bounded(double y) noexcept {
    // y in [-64, 64]: 2^n by the exponent field, 2^f (f in [0, 1)) by the Taylor series of e^(f ln 2).
    const double n = scalar::floor(y);
    const double f = (y - n) * .6931471805599453;
    double sum = 1., term = 1.;
    for (unsigned k = 1; k <= 16; ++k) {
        term *= f / double(k);
        sum += term;
    }
    const std::uint64_t bits = std::uint64_t(int(n) + 1023) << 52;
    double scale;
    __builtin_memcpy(&scale, &bits, sizeof scale);
    return sum * scale;
}
// The colour classes of sky_textures.STAR_LANES (linear target ratios, brightest channel 1; 7 and above: white).
inline void lane_tint(unsigned lane, double out[3]) noexcept {
    static constexpr double tints[lane_count][3] = {
        {1., 1., 1.},    {.62, .76, 1.},  {1., .58, .48}, {1., .76, .52},
        {1., .93, .66},  {.72, 1., .78},  {.62, .95, 1.}, {1., 1., 1.}};
    const double* t = tints[lane < lane_count ? lane : lane_count - 1];
    out[0] = t[0];
    out[1] = t[1];
    out[2] = t[2];
}
struct BuildStats {
    unsigned stars = 0;
    double peak_max = 0., peak_median_decoded = 0., energy_max = 0., support_max = 0.;
};
// Writes the 4 * N vertices of the seed's field into `out` (room for `capacity_stars` stars). Returns N (0: refused,
// no seed or no room). Runs once per seed on the render thread.
inline unsigned build(std::uint32_t seed, float density, Vertex* out, unsigned capacity_stars,
                      BuildStats* stats = nullptr) noexcept {
    if (!(seed & seed_valid) || !out) return 0;
    const unsigned n = star_count(seed_k(seed), density);
    if (n > capacity_stars) return 0;
    Rng rng(seed);
    double class_tint[3];
    lane_tint(seed_lane(seed), class_tint);
    const double fraction_min = centre_fraction(half_support_min);
    double peak_max = 0., energy_max = 0., support_max = 0.;
    for (unsigned i = 0; i < n; ++i) {
        // Direction: Marsaglia (1972), uniform on the sphere from a point in the unit disc.
        double x, y, s;
        do {
            rng.uniform(&x);
            rng.uniform(&y);
            x = 2. * x - 1.;
            y = 2. * y - 1.;
            s = x * x + y * y;
        } while (!(s > 1e-12 && s < 1.));
        const double r = double(scalar::sqrt(float(1. - s)));
        const float dir[3] = {float(2. * x * r), float(2. * y * r), float(1. - 2. * s)};
        // Brightness: decoded peak 1 / X, X uniform in [1, x_max]; the target peak through the decode gamma.
        double u;
        rng.uniform(&u);
        const double decoded = 1. / (1. + u * (double(x_max) - 1.));
        const double peak = exp2_bounded(log2_positive(decoded) / decode_gamma);
        const double energy = peak / fraction_min;
        double a;
        half_support(energy, &a);
        // Tint: the class colour mixed with white by a per-star share in [0.25, 0.75], brightest channel 255.
        double share;
        rng.uniform(&share);
        share = .25 + .5 * share;
        double c[3], top = 0.;
        for (unsigned k = 0; k < 3; ++k) {
            c[k] = 1. + (class_tint[k] - 1.) * share;
            if (c[k] > top) top = c[k];
        }
        std::uint32_t bytes[3];
        for (unsigned k = 0; k < 3; ++k) bytes[k] = std::uint32_t(c[k] / top * 255. + .5);
        const std::uint32_t tint = 0xff000000u | bytes[0] << 16 | bytes[1] << 8 | bytes[2];
        const float h = float(a) + quad_margin;
        const float corners[4][2] = {{-h, -h}, {h, -h}, {-h, h}, {h, h}};
        for (unsigned v = 0; v < 4; ++v) {
            Vertex& o = out[i * 4 + v];
            o.dir[0] = dir[0];
            o.dir[1] = dir[1];
            o.dir[2] = dir[2];
            o.corner[0] = corners[v][0];
            o.corner[1] = corners[v][1];
            o.energy = float(energy);
            o.tint = tint;
        }
        if (peak > peak_max) peak_max = peak;
        if (energy > energy_max) energy_max = energy;
        if (a > support_max) support_max = a;
    }
    if (stats) {
        stats->stars = n;
        stats->peak_max = peak_max;
        stats->peak_median_decoded = 2. / (1. + double(x_max)); // the law's median (decoded)
        stats->energy_max = energy_max;
        stats->support_max = support_max;
    }
    return n;
}
// The static index list of the quads: 0 1 2, 2 1 3 per star (16-bit; the buffer is built once for stars_max).
inline void build_indices(std::uint16_t* out, unsigned stars) noexcept {
    for (unsigned i = 0; i < stars; ++i) {
        const std::uint16_t b = std::uint16_t(i * 4);
        out[i * 6 + 0] = b;
        out[i * 6 + 1] = std::uint16_t(b + 1);
        out[i * 6 + 2] = std::uint16_t(b + 2);
        out[i * 6 + 3] = std::uint16_t(b + 2);
        out[i * 6 + 4] = std::uint16_t(b + 1);
        out[i * 6 + 5] = std::uint16_t(b + 3);
    }
}

// ---- Vertex constants (c252-c255) ----
// The view rotation r (camera_reprojection.h CameraState::r, row-vector: view_j = sum_i world_i r[i*3+j]), the
// projection terms m00, m11, m20, m21 (P[0], P[5], P[8], P[9]; P[11] = 1, no P[10]/P[14]: the far plane is z = w *
// far_z), the TAA jitter in raster px (MotionOutput jitter_, the sign of fade_region_math.h jitter_rows) and the target
// size. Rows: c252 = clip x, c253 = clip y, c254 = clip w (world direction dot), c255 = (2/W, 2/H, far_z, quad_margin).
constexpr float far_z = .99999f;
inline bool vertex_constants(const float r[9], float m00, float m11, float m20, float m21, float jitter_x_px,
                             float jitter_y_px, unsigned width, unsigned height, float out[16]) noexcept {
    if (!width || !height) return false;
    const float jx = 2.f * jitter_x_px / float(width), jy = -2.f * jitter_y_px / float(height);
    for (unsigned i = 0; i < 3; ++i) {
        const float vx = r[i * 3 + 0], vy = r[i * 3 + 1], vz = r[i * 3 + 2];
        out[0 + i] = vx * m00 + vz * (m20 + jx);
        out[4 + i] = vy * m11 + vz * (m21 + jy);
        out[8 + i] = vz;
    }
    out[3] = out[7] = out[11] = 0.f;
    out[12] = 2.f / float(width);
    out[13] = 2.f / float(height);
    out[14] = far_z;
    out[15] = quad_margin;
    for (unsigned i = 0; i < 16; ++i)
        if (!(out[i] == out[i] && out[i] < 3.0e38f && out[i] > -3.0e38f)) return false;
    return true;
}
} // namespace x3m::sky_stars::core
