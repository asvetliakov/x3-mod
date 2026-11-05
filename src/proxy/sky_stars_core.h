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
//   steps        = log_2.5(x_max / X)                 (flux steps above the dimmest star, [0, 4])
//   a            = min_size + 0.35 size steps         (sky_stars_min_size, sky_stars_size; on the 1/16 px grid)
//   energy       = gain target peak / centre_fraction(a)   (gain = sky_stars_brightness; the sum over the covered
//                                                        pixels at any sub-pixel position; the centred peak is gain x
//                                                        target peak whatever a, so a larger footprint adds light)
// then a grows in 1/16 px steps while the centred peak energy x centre_fraction(a) exceeds peak_cap (2.0, decoded 4.6);
// at half_support_max the energy is lowered to the cap instead. The twinkle multiplies after the cap: a twinkling star's
// instantaneous peak reaches peak_cap x (1 + amplitude).
// Twinkle: about one star in four (a second generator stream, not by brightness) carries a rate n / 128 Hz, n in
// [52, 192], and a phase; the vertex program scales the energy by 1 + amplitude sin(2 pi (rate t + phase)), t the wall
// clock modulo twinkle_period_s (c251, twinkle_constants). Energy only: the footprint does not change.
// Kernel: separable Beta(3,3) density over [-a, a] px per axis (Gaussian-shaped, sigma = 0.378 a), whose CDF is the
// quintic smootherstep C(t) = t^3 (t (6 t - 15) + 10), t = (x + a) / (2 a). A pixel at offset u (pixel centre minus star
// centre) receives (C(t(u + 0.5)) - C(t(u - 0.5))) per axis; the sum over every pixel telescopes to C(1) - C(0) = 1
// exactly when every pixel whose area meets [-a, a] is rasterised, which the quad's half extent a + quad_margin
// guarantees. With min_size 1, size 0 and gain 1 the law is the first one's: a = 1 px, energy = peak / centre_fraction(1).
// Clustering (sky_stars_clustering = c): the directions follow a seeded density on the sphere,
//   density(d) = min(bound, max(floor, 1 + sum_clusters c w_i S_i(d) - sum_voids c v_j S_j(d))),
//   S(d) = smootherstep((d.n - cos r) / (1 - cos r)) (1 at the cap's centre, 0 at its rim)
// 6-10 cluster caps (radius uniform in 8-35 degrees, w uniform in [1, 4]) and 2-4 void caps (radius 10 + 15 sqrt(u)
// degrees, so 10-25 leaning large; v in [2, 3]: at c = 1 a void sits on the floor over the inner part of its cap, at c = 2
// over most of it). A void's centre is drawn outside every cluster cap (bounded retries). floor = 0.08 x the void-free
// mean 1 + sum c w_i (1 - cos r_i) / 4 (the cap average of S is (1 - cos r) / 4), so a void keeps a few stars. bound =
// the largest density at the cluster centres and the weighted midpoints of the cluster pairs. A star's first candidate is
// the uniform draw of the first law; it is kept with probability density / bound and otherwise replaced by a uniform
// candidate of a third stream, at most max_candidates per star (the last one is then kept, so build() always ends).
// c = 0 skips the test: the first law's directions, byte for byte. The caps come from their own stream, so the layout
// does not move with c, and brightness, tint and twinkle never depend on it.
#include "lod_texture_mark.h"
#include "sse_scalar.h"
#include <cstdint>

namespace x3m::sky_stars::core {
constexpr unsigned payload_sky_stars = lod_texture_mark::payload_sky_stars; // descriptor bits 24-31 of a star texture
constexpr unsigned lane_count = 8;            // sky_textures.STAR_LANES: white blue red orange yellow green cyan, 7 other
constexpr unsigned stars_per_k = 48, stars_min = 512, stars_max = 16384;
constexpr float density_default = 2.f, density_min = .25f, density_max = 4.f;         // sky_stars_density
constexpr float brightness_default = 1.5f, brightness_min = .5f, brightness_max = 8.f; // sky_stars_brightness (gain)
constexpr float size_default = 1.f, size_min = 0.f, size_max = 3.f;                   // sky_stars_size
constexpr float min_size_default = 1.5f, min_size_min = 1.f, min_size_max = 3.f;      // sky_stars_min_size (px)
constexpr float twinkle_default = .3f, twinkle_min = 0.f, twinkle_max = 1.f;          // sky_stars_twinkle (amplitude)
constexpr float clustering_default = 1.2f, clustering_min = 0.f, clustering_max = 2.f; // sky_stars_clustering
constexpr unsigned vertex_stride = 32, vertices_per_star = 4, indices_per_star = 6;
constexpr unsigned vertex_capacity = stars_max * vertices_per_star; // 65536: 16-bit indices suffice
constexpr float flux_step = 2.5f, flux_steps = 4.f, x_max = 39.0625f; // 2.5^4
constexpr double decode_gamma = 2.2;                                    // the installed hdr_decode (gamma2.2)
constexpr float half_support_min = 1.f; // px: the 2 px footprint of the first law (min_size_min)
constexpr float half_support_max = 8.f; // px: the hard bound of the support (the defaults reach 2.875)
constexpr float quad_margin = .5625f;   // px beyond the support: every pixel with weight has its centre inside the quad
constexpr double peak_cap = 2.;         // target value of the largest centred pixel (decoded 2^2.2 = 4.6)
constexpr double size_slope = .35;      // px of half-width per flux step at size 1 (4 steps: 1.4 px)
constexpr double log2_flux_step = 1.3219280948873624; // log2(2.5)
// Twinkle lanes (Vertex::twinkle, D3DCOLOR): R = rate n (n / 128 Hz), G = phase (G / 255 cycles), B = 255 on a twinkling
// star; 0 on the others. Every rate is a multiple of 1/128 Hz, so 128 s is a common period: t wraps there without a jump.
constexpr unsigned twinkle_rate_min = 52, twinkle_rate_max = 192; // 0.41 .. 1.5 Hz
constexpr std::uint64_t twinkle_period_s = 128;
// Clustering law (the header comment): cap counts and ranges, the floor's share of the mean, the candidate cap.
constexpr unsigned clusters_min = 6, clusters_max = 10, voids_min = 2, voids_max = 4;
constexpr unsigned caps_max = clusters_max + voids_max;
constexpr double cluster_radius_min_deg = 8., cluster_radius_max_deg = 35., cluster_weight_min = 1., cluster_weight_max = 4.;
constexpr double void_radius_min_deg = 10., void_radius_max_deg = 25., void_weight_min = 2., void_weight_max = 3.;
constexpr double density_floor_share = .08;
constexpr unsigned void_centre_tries = 16; // draws of a void centre outside every cluster cap
constexpr unsigned max_candidates = 64; // per star; the last candidate is kept on exhaustion
constexpr unsigned sphere_bins = 48;    // the equal-area bins of BuildStats::cluster_max_min (6 z bands x 8 sectors)
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
// The accepted settings: a finite value in the setting's range replaces *out (a refusal leaves *out, the default).
inline bool accept_range(float v, float lo, float hi, float* out) noexcept {
    if (!(v >= lo && v <= hi)) return false;
    *out = v;
    return true;
}
inline bool accept_density(float v, float* out) noexcept { return accept_range(v, density_min, density_max, out); }
inline bool accept_brightness(float v, float* out) noexcept { return accept_range(v, brightness_min, brightness_max, out); }
inline bool accept_size(float v, float* out) noexcept { return accept_range(v, size_min, size_max, out); }
inline bool accept_min_size(float v, float* out) noexcept { return accept_range(v, min_size_min, min_size_max, out); }
inline bool accept_twinkle(float v, float* out) noexcept { return accept_range(v, twinkle_min, twinkle_max, out); }
inline bool accept_clustering(float v, float* out) noexcept { return accept_range(v, clustering_min, clustering_max, out); }
// The generator seed of a field: seed64 = mark_seed ^ (sector_key x 0x9e3779b97f4a7c15), folded to 32 bits (low ^ high).
// sector_key 0 (no sector known yet) returns mark_seed itself; a known sector supplies sector_key::core::key_of(x, y)
// (src/proxy/sector_key_core.h), never 0.
inline std::uint32_t generator_seed(std::uint32_t mark_seed, std::uint64_t sector_key) noexcept {
    const std::uint64_t seed64 = std::uint64_t(mark_seed) ^ (sector_key * 0x9e3779b97f4a7c15ull);
    return std::uint32_t(seed64) ^ std::uint32_t(seed64 >> 32);
}
// The settings the vertex buffer depends on (the twinkle amplitude is a constant, c251: no rebuild).
struct Look {
    float density = density_default, brightness = brightness_default, size = size_default, min_size = min_size_default;
    float clustering = clustering_default;
};
inline bool same_look(const Look& a, const Look& b) noexcept {
    return a.density == b.density && a.brightness == b.brightness && a.size == b.size && a.min_size == b.min_size &&
           a.clustering == b.clustering;
}
// The vertex buffer's identity (SkyStarsPass::prepare's cached-build check): the mark seed, the look and the sector key
// of generator_seed; the field is rebuilt when any of them differs from the built one.
inline bool same_build(std::uint32_t seed, const Look& look, std::uint64_t sector_key, std::uint32_t built_seed,
                       const Look& built_look, std::uint64_t built_key) noexcept {
    return seed == built_seed && sector_key == built_key && same_look(look, built_look);
}
// Out-of-range members back to their defaults (the builder's own guard; the settings path has refused them already).
inline Look sanitized(const Look& v) noexcept {
    Look out;
    accept_density(v.density, &out.density);
    accept_brightness(v.brightness, &out.brightness);
    accept_size(v.size, &out.size);
    accept_min_size(v.min_size, &out.min_size);
    accept_clustering(v.clustering, &out.clustering);
    return out;
}
// N = clamp(round(K x 48 x density), 512, 16384).
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
    std::uint32_t twinkle; // D3DCOLOR lanes: rate n (R), phase (G), on (B); 0 = steady
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
// The cap search: *a (the start) grows in 1/16 px steps with the energy fixed until the centred peak
// energy x centre_fraction(a) is at most peak_cap. half_support_max stays a hard bound: a star still above the cap there
// (a large size term with a high gain) has its energy lowered to peak_cap / centre_fraction(a), so the centred peak is at
// most peak_cap for every legal setting.
inline void half_support(double* energy, double* a) noexcept {
    double s = *a;
    while (s < half_support_max && *energy * centre_fraction(s) > peak_cap + 1e-9)
        s += 1. / 16.;
    const double fraction = centre_fraction(s);
    if (*energy * fraction > peak_cap) *energy = peak_cap / fraction;
    *a = s;
}
// The support and energy of a star of target peak `peak` (<= 1), `steps` flux steps above the dimmest ([0, 4]): the size
// term on the 1/16 px grid, the energy that keeps the centred peak at gain x peak there, then the cap search.
inline void star_support(double peak, double steps, const Look& look, double* a_out, double* energy_out) noexcept {
    double a = double(look.min_size) + size_slope * double(look.size) * steps;
    a = scalar::floor(a * 16. + .5) * (1. / 16.);
    if (a > double(half_support_max)) a = half_support_max;
    double energy = double(look.brightness) * peak / centre_fraction(a);
    half_support(&energy, &a);
    *a_out = a;
    *energy_out = energy;
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
    unsigned stars = 0, twinkling = 0;
    // peak_max: the largest centred pixel (target value, <= peak_cap); support_max: the largest half-width a in px.
    double peak_max = 0., peak_median_decoded = 0., energy_max = 0., support_max = 0.;
    // cluster_max_min: the fullest of the 48 equal-area bins over the emptiest (an empty bin counts as 1); reject_rate:
    // rejected candidates / all candidates (0 at clustering 0); exhausted: stars that kept their last candidate.
    double cluster_max_min = 0., reject_rate = 0.;
    unsigned bin_min = 0, bin_max = 0, candidates = 0, exhausted = 0;
};
// cos x for |x| <= pi / 2 by its Taylor series (no CRT): |error| < 1e-12.
[[gnu::always_inline]] inline double cos_bounded(double x) noexcept {
    const double x2 = x * x;
    double sum = 1., term = 1.;
    for (unsigned k = 2; k <= 20; k += 2) {
        term *= -x2 / double((k - 1) * k);
        sum += term;
    }
    return sum;
}
// The equal-area bin of a unit direction: 6 bands of equal height in z (Archimedes: equal area) x 8 sectors of 45
// degrees in longitude, told apart by the signs and |x| against |y| (no atan2; flag arithmetic, no data-dependent
// branch: the sectors of random directions mispredict).
inline unsigned sphere_bin(const float d[3]) noexcept {
    int band = int((double(d[2]) + 1.) * 3.);
    band = band < 0 ? 0 : band > 5 ? 5 : band;
    const float x = d[0], y = d[1], ax = x < 0.f ? -x : x, ay = y < 0.f ? -y : y;
    // Quadrant q (0: x, y >= 0; 1: x < 0 <= y; 2: x, y < 0; 3: y < 0 <= x), then its second 45 degrees.
    const unsigned ny = y < 0.f, nx = x < 0.f, q = ny * 2u + (nx ^ ny), odd = q & 1u;
    const unsigned half = (odd & unsigned(ay <= ax)) | ((odd ^ 1u) & unsigned(ay >= ax));
    return unsigned(band) * 8u + q * 2u + half;
}
// A uniform direction on the sphere: Marsaglia (1972), from a point in the unit disc.
inline void uniform_direction(Rng& rng, float dir[3]) noexcept {
    double x, y, s;
    do {
        rng.uniform(&x);
        rng.uniform(&y);
        x = 2. * x - 1.;
        y = 2. * y - 1.;
        s = x * x + y * y;
    } while (!(s > 1e-12 && s < 1.));
    const double r = double(scalar::sqrt(float(1. - s)));
    dir[0] = float(2. * x * r);
    dir[1] = float(2. * y * r);
    dir[2] = float(1. - 2. * s);
}
// The clustering law of one field (the header comment): the caps of the field's cap stream, scaled by c.
struct ClusterField {
    struct Cap {
        double n[3], cos_r, inv_span, weight; // weight > 0: a cluster, < 0: a void
    };
    Cap caps[caps_max];
    unsigned count = 0;
    double floor = 0., bound = 1.;
    // The density at a unit direction before the clip at the bound (>= floor).
    [[gnu::always_inline]] double density(const float d[3]) const noexcept {
        double sum = 1.;
        for (unsigned i = 0; i < count; ++i) {
            const Cap& cap = caps[i];
            const double dot = double(d[0]) * cap.n[0] + double(d[1]) * cap.n[1] + double(d[2]) * cap.n[2];
            if (dot > cap.cos_r) sum += cap.weight * smootherstep((dot - cap.cos_r) * cap.inv_span);
        }
        return sum < floor ? floor : sum;
    }
};
// Draws the caps (the stream's draws do not depend on c), then the floor and the bound at clustering c > 0.
inline void cluster_field(Rng& stream, double c, ClusterField* f) noexcept {
    constexpr double degree = 3.14159265358979323846 / 180.;
    double u;
    const unsigned clusters = clusters_min + stream.next() % (clusters_max - clusters_min + 1u);
    const unsigned voids = voids_min + stream.next() % (voids_max - voids_min + 1u);
    double mean = 1.;
    f->count = clusters + voids;
    for (unsigned i = 0; i < f->count; ++i) {
        ClusterField::Cap& cap = f->caps[i];
        const bool cluster = i < clusters;
        // A void's centre lies outside every cluster cap (voids sit between the clusters; at most void_centre_tries
        // draws, the last kept).
        float n[3];
        for (unsigned tries = 0;;) {
            uniform_direction(stream, n);
            bool inside = false;
            for (unsigned j = 0; !cluster && j < clusters; ++j) {
                const ClusterField::Cap& b = f->caps[j];
                inside = inside || double(n[0]) * b.n[0] + double(n[1]) * b.n[1] + double(n[2]) * b.n[2] > b.cos_r;
            }
            if (!inside || ++tries == void_centre_tries) break;
        }
        cap.n[0] = n[0];
        cap.n[1] = n[1];
        cap.n[2] = n[2];
        stream.uniform(&u);
        const double radius = cluster ? cluster_radius_min_deg + u * (cluster_radius_max_deg - cluster_radius_min_deg)
                                      : void_radius_min_deg + double(scalar::sqrt(float(u))) *
                                                                  (void_radius_max_deg - void_radius_min_deg);
        cap.cos_r = cos_bounded(radius * degree);
        cap.inv_span = 1. / (1. - cap.cos_r);
        stream.uniform(&u);
        cap.weight = cluster ? c * (cluster_weight_min + u * (cluster_weight_max - cluster_weight_min))
                             : -c * (void_weight_min + u * (void_weight_max - void_weight_min));
        if (cluster) mean += cap.weight * (1. - cap.cos_r) * .25;
    }
    f->floor = density_floor_share * mean;
    // The bound: the largest density at the cluster centres and at the weighted midpoints of every pair of clusters.
    // The law is the density clipped there (a candidate above it is always kept), so the sampling is exact for the
    // clipped law; the clip only flattens the crest where overlapping clusters peak between their probes.
    double bound = 1.;
    for (unsigned i = 0; i < clusters; ++i) {
        const ClusterField::Cap& a = f->caps[i];
        for (unsigned j = i; j < clusters; ++j) {
            const ClusterField::Cap& b = f->caps[j];
            const double m[3] = {a.n[0] * a.weight + b.n[0] * b.weight, a.n[1] * a.weight + b.n[1] * b.weight,
                                 a.n[2] * a.weight + b.n[2] * b.weight};
            const double length2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
            if (!(length2 > 1e-12)) continue;
            const double inv = 1. / double(scalar::sqrt(float(length2)));
            const float probe[3] = {float(m[0] * inv), float(m[1] * inv), float(m[2] * inv)};
            const double v = f->density(probe);
            if (v > bound) bound = v;
        }
    }
    f->bound = bound;
}
// The twinkle lanes of one star (one draw of the twinkle stream per star): one in four on; rate and phase from the
// same word.
inline std::uint32_t twinkle_lanes(Rng& stream) noexcept {
    const std::uint32_t r = stream.next();
    if (r >> 30) return 0;
    const std::uint32_t rate = twinkle_rate_min + (((r & 0xffffu) * (twinkle_rate_max - twinkle_rate_min + 1u)) >> 16);
    const std::uint32_t phase = (r >> 16) & 255u;
    return rate << 16 | phase << 8 | 255u;
}
// Writes the 4 * N vertices of the seed's field into `out` (room for `capacity_stars` stars). Returns N (0: refused,
// no seed or no room). Runs once per (seed, look) on the render thread. Directions, brightness and tint come from the
// seed's stream as in the first law and the twinkle lanes from a second stream, so the field is the same in any look
// except clustering, which moves only the directions of the rejected first candidates (the cap stream, a third).
// sector_key: the per-sector part of the generator seed (generator_seed; 0 before any sector is known: the mark's field).
inline unsigned build(std::uint32_t seed, const Look& requested, Vertex* out, unsigned capacity_stars,
                      BuildStats* stats = nullptr, std::uint64_t sector_key = 0) noexcept {
    if (!(seed & seed_valid) || !out) return 0;
    const Look look = sanitized(requested);
    const unsigned n = star_count(seed_k(seed), look.density);
    if (n > capacity_stars) return 0;
    const std::uint32_t field = generator_seed(seed, sector_key);
    Rng rng(field), twinkle_stream(field ^ 0x7417c1e5u), cap_stream(field ^ 0x3c6ef372u);
    const bool clustered = look.clustering > 0.f;
    ClusterField law;
    if (clustered) cluster_field(cap_stream, double(look.clustering), &law);
    double class_tint[3];
    lane_tint(seed_lane(seed), class_tint);
    double peak_max = 0., energy_max = 0., support_max = 0.;
    unsigned twinkling = 0, candidates = 0, exhausted = 0;
    unsigned bins[sphere_bins] = {};
    for (unsigned i = 0; i < n; ++i) {
        // Direction: the first law's uniform draw; with clustering kept with probability density / bound, otherwise
        // replaced by a candidate of the cap stream (at most max_candidates per star, the last one kept).
        float dir[3];
        uniform_direction(rng, dir);
        ++candidates;
        if (clustered)
            for (unsigned tries = 1;; ++tries) {
                double u;
                cap_stream.uniform(&u);
                if (u * law.bound < law.density(dir)) break;
                if (tries == max_candidates) {
                    ++exhausted;
                    break;
                }
                uniform_direction(cap_stream, dir);
                ++candidates;
            }
        ++bins[sphere_bin(dir)];
        // Brightness: decoded peak 1 / X, X uniform in [1, x_max]; the target peak through the decode gamma.
        double u;
        rng.uniform(&u);
        const double decoded = 1. / (1. + u * (double(x_max) - 1.));
        // One logarithm per star: steps = log_2.5(x_max decoded) = 4 + log2(decoded) / log2(2.5) (x_max = 2.5^4).
        const double log2_decoded = log2_positive(decoded);
        const double peak = exp2_bounded(log2_decoded / decode_gamma);
        double steps = double(flux_steps) + log2_decoded * (1. / log2_flux_step);
        steps = steps < 0. ? 0. : steps > double(flux_steps) ? double(flux_steps) : steps;
        double a, energy;
        star_support(peak, steps, look, &a, &energy);
        const std::uint32_t twinkle = twinkle_lanes(twinkle_stream);
        twinkling += twinkle != 0;
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
            o.twinkle = twinkle;
        }
        const double centred = energy * centre_fraction(a);
        if (centred > peak_max) peak_max = centred;
        if (energy > energy_max) energy_max = energy;
        if (a > support_max) support_max = a;
    }
    if (stats) {
        unsigned lo = bins[0], hi = bins[0];
        for (unsigned b = 1; b < sphere_bins; ++b) {
            lo = bins[b] < lo ? bins[b] : lo;
            hi = bins[b] > hi ? bins[b] : hi;
        }
        stats->bin_min = lo;
        stats->bin_max = hi;
        stats->cluster_max_min = double(int(hi)) / double(int(lo ? lo : 1u));
        stats->candidates = candidates;
        stats->exhausted = exhausted;
        stats->reject_rate = double(int(candidates - n)) / double(int(candidates));
        stats->stars = n;
        stats->twinkling = twinkling;
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

// ---- Vertex constants (c251 twinkle, c252-c255 projection) ----
// c251 = (t, amplitude, 0, 0): t the wall clock in seconds modulo twinkle_period_s from QueryPerformanceCounter ticks
// and frequency (wall time, not game time: SETA must not speed the twinkle up), amplitude = sky_stars_twinkle. The
// conversion uses 31-bit signed halves only (cvtsi2sd; a 64-bit or unsigned conversion is an x87 fild on i686).
inline bool twinkle_constants(std::uint64_t ticks, std::uint64_t frequency, float amplitude, float out[4]) noexcept {
    out[0] = out[1] = out[2] = out[3] = 0.f;
    if (!frequency || frequency >= (std::uint64_t(1) << 50)) return false;
    if (!(amplitude >= twinkle_min && amplitude <= twinkle_max)) return false;
    const std::uint64_t wrapped = ticks % (frequency * twinkle_period_s);
    const double t = (double(int(std::uint32_t(wrapped >> 31))) * 2147483648. +
                      double(int(std::uint32_t(wrapped) & 0x7fffffffu))) /
                     (double(int(std::uint32_t(frequency >> 31))) * 2147483648. +
                      double(int(std::uint32_t(frequency) & 0x7fffffffu)));
    out[0] = float(t);
    out[1] = amplitude;
    return true;
}
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
