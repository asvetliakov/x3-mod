// Host check of src/proxy/sky_stars_core.h with src/proxy/lod_texture_mark.h and the installed exposure meter
// (src/renderer/exposure.cpp) for verification/analysis/test_sky_stars.py. Prints one JSON object: the mark parse of a
// payload-2 descriptor and the table round trip of K, the seed and N derivation, the settings' ranges, the vertex-buffer
// builder (count, stride, magnitude law, peak cap, size law, the first law at min_size 1 / size 0 / gain 1 / clustering
// 0, the clustering law in 48 equal-area bins, the sector-key seed fold and the cached-build check, twinkle lanes and
// c251, determinism), the
// kernel's energy at sub-pixel offsets, and the meter replay on star-only
// frames (invariant 5 of docs/architecture/sector-background-modern.md). No D3D, no Windows.
#include "../../src/proxy/sky_stars_core.h"
#include "../../src/renderer/exposure.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace core = x3m::sky_stars::core;
namespace mark = x3m::lod_texture_mark;
namespace r = x3m::renderer;

static unsigned checks = 0, failures = 0;
static std::string failed;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        if (failed.size() < 2000) failed += std::string(failed.empty() ? "" : ",") + what;
    }
}
static void put32(unsigned char* p, std::uint32_t v) {
    p[0] = v & 255;
    p[1] = (v >> 8) & 255;
    p[2] = (v >> 16) & 255;
    p[3] = v >> 24;
}
static std::vector<unsigned char> header(unsigned slot, unsigned lane, unsigned k, unsigned payload, unsigned version) {
    std::vector<unsigned char> h(128, 0);
    put32(&h[0], 0x20534444u);
    put32(&h[4], 124u);
    put32(&h[12], 1024u);
    put32(&h[16], 1024u);
    put32(&h[32], mark::magic);
    put32(&h[36], version);
    put32(&h[40], slot | lane << 8 | k << 16 | payload << 24);
    return h;
}

struct Splat {
    double sum[3] = {0, 0, 0}, peak = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the quad's pixel bounds
};
// The pixel program on the CPU: every pixel whose centre lies inside the quad of half extent h around (cx, cy).
static Splat splat(const core::Vertex& v, double cx, double cy, unsigned width, unsigned height, float* rgb) {
    Splat s;
    const double h = v.corner[0] < 0 ? -v.corner[0] : v.corner[0], a = h - core::quad_margin;
    const double tint[3] = {double((v.tint >> 16) & 255) / 255., double((v.tint >> 8) & 255) / 255.,
                            double(v.tint & 255) / 255.};
    s.x0 = int(std::ceil(cx - h));
    s.x1 = int(std::floor(cx + h));
    s.y0 = int(std::ceil(cy - h));
    s.y1 = int(std::floor(cy + h));
    for (int y = s.y0; y <= s.y1; ++y)
        for (int x = s.x0; x <= s.x1; ++x) {
            const double w = core::axis_weight(double(x) - cx, a) * core::axis_weight(double(y) - cy, a) * v.energy;
            for (unsigned c = 0; c < 3; ++c) s.sum[c] += w * tint[c];
            if (w > s.peak) s.peak = w;
            if (rgb && x >= 0 && y >= 0 && unsigned(x) < width && unsigned(y) < height)
                for (unsigned c = 0; c < 3; ++c) rgb[(std::size_t(y) * width + unsigned(x)) * 3 + c] += float(w * tint[c]);
        }
    return s;
}

struct Meter {
    double ev_target, ev_key, ev_limit, p99_max_log, lit_fraction;
    unsigned tiles, lit, tile_w, tile_h, visible;
    bool neutral;
    double frame_max;
};
// The installed meter (hdr_pass.cpp chain + exposure.cpp statistic) on an engine-space RGB frame.
static Meter meter(const std::vector<float>& rgb, unsigned width, unsigned height, unsigned visible) {
    r::ExposureParams p;
    p.ev_max = 1.3f; // the schema default of hdr_ev_max
    std::vector<float> mean(std::size_t(width) * height), max(mean.size()), scratch((width / 4 + 1) * (height / 4 + 1));
    double frame_max = 0;
    for (std::size_t i = 0; i < mean.size(); ++i) {
        const float px[3] = {rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]};
        mean[i] = max[i] = r::meter_level0(px, r::ExposureDecode::Gamma22, p);
        for (float c : px) frame_max = c > frame_max ? c : frame_max;
    }
    unsigned w = width, h = height;
    r::reduce_chain(mean.data(), max.data(), scratch.data(), w, h, 4, r::kMeterTileMax);
    std::vector<float> weights(std::size_t(w) * h);
    r::tile_weights(weights.data(), w, h, p.meter_edge_weight);
    std::vector<r::TileSample> work(std::size_t(w) * h);
    const auto m = r::meter_statistics(mean.data(), max.data(), weights.data(), w * h, work.data(), p);
    const auto t = r::exposure_target(m, p);
    return {t.ev_target, t.ev_key, t.ev_limit, m.p99_max_log, m.lit_fraction, m.tiles, m.lit, w, h, visible, m.neutral,
            frame_max};
}
// A star-only frame: the seed's field through a synthetic camera (identity view, vertical FOV 60 degrees).
static Meter star_frame(std::uint32_t seed, const core::Look& look, unsigned width, unsigned height, double* worst_peak) {
    std::vector<core::Vertex> vb(core::vertex_capacity);
    const unsigned n = core::build(seed, look, vb.data(), core::stars_max);
    std::vector<float> rgb(std::size_t(width) * height * 3, 0.f);
    const double m11 = 1. / std::tan(30. * 3.14159265358979 / 180.), m00 = m11 * height / width;
    unsigned visible = 0;
    *worst_peak = 0;
    for (unsigned i = 0; i < n; ++i) {
        const auto& v = vb[i * 4];
        if (v.dir[2] <= 0.05f) continue;
        const double cx = (v.dir[0] * m00 / v.dir[2] + 1.) * .5 * width, cy = (1. - v.dir[1] * m11 / v.dir[2]) * .5 * height;
        if (cx < -8 || cy < -8 || cx > width + 8 || cy > height + 8) continue;
        ++visible;
        const Splat s = splat(v, cx, cy, width, height, rgb.data());
        if (s.peak > *worst_peak) *worst_peak = s.peak;
    }
    return meter(rgb, width, height, visible);
}

int main() {
    // ---- 1. Mark parse and seed: payload 2 carries slot/lane/K; anything else is not a star seed.
    {
        const auto h = header(0xa7, 1, 65, 2, 1);
        mark::Mark m;
        check(mark::parse(h.data(), h.size(), m), "parse_payload2");
        check(m.slot == 0xa7 && m.lane == 1 && m.k == 65 && m.payload == 2 && m.version == 1, "fields_payload2");
        const std::uint32_t seed = core::seed_of(m);
        check(seed == (0xa7u | 1u << 8 | 65u << 16 | core::seed_valid), "seed_payload2");
        check(core::seed_slot(seed) == 0xa7 && core::seed_lane(seed) == 1 && core::seed_k(seed) == 65, "seed_unpack");
        check(mark::lane_of(m) == mark::lane_none, "strength_lane_untouched");
        const auto zero = header(0, 0, 0, 2, 1);
        check(mark::parse(zero.data(), zero.size(), m) && core::seed_of(m) == core::seed_valid, "seed_all_zero_valid");
        const auto strength = header(3, 3, 2, 1, 1);
        check(mark::parse(strength.data(), strength.size(), m) && !core::seed_of(m) &&
                  mark::lane_of(m) == mark::lane_strength_alpha,
              "strength_mark_not_a_seed");
        const auto other_version = header(1, 2, 3, 2, 2);
        check(mark::parse(other_version.data(), other_version.size(), m) && !core::seed_of(m), "version_refused");
        const auto nebula = header(1, 2, 3, 3, 1);
        check(mark::parse(nebula.data(), nebula.size(), m) && !core::seed_of(m), "payload3_refused");
        auto unmarked = header(1, 2, 3, 2, 1);
        put32(&unmarked[32], 0);
        check(!mark::parse(unmarked.data(), unmarked.size(), m), "unmarked_refused");
        check(!mark::parse(h.data(), 127, m), "short_refused");
        // The table keeps K through an erase that re-inserts the probe run.
        static mark::Table<256> table;
        const unsigned before = mark::live_marks.load();
        int objects[64];
        for (unsigned i = 0; i < 64; ++i) table.insert(&objects[i], 100 + i, std::uint8_t(i & 7), std::uint8_t(i), nullptr,
                                                       std::uint8_t(200 - i));
        for (unsigned i = 0; i < 64; i += 3) table.erase(&objects[i]);
        bool kept = true;
        for (unsigned i = 0; i < 64; ++i) {
            auto* e = table.find(&objects[i]);
            if (i % 3 == 0) kept = kept && !e;
            else kept = kept && e && e->k == std::uint8_t(200 - i) && e->slot == i && e->lane == (i & 7) && e->identity == 100 + i;
        }
        check(kept, "table_k_round_trip");
        check(mark::live_marks.load() == before + table.count, "live_marks_exact");
    }
    // ---- 2. N = clamp(round(K x 48 x density), 512, 16384); a refused density counts as the default 2.
    const unsigned n0 = core::star_count(0, 1.f), n65 = core::star_count(65, 1.f), n143 = core::star_count(143, 1.f),
                   n255 = core::star_count(255, 1.f), n65q = core::star_count(65, .25f), n65x4 = core::star_count(65, 4.f),
                   n65bad = core::star_count(65, 9.f), n65d = core::star_count(65, core::density_default),
                   n255d = core::star_count(255, core::density_default), n171x2 = core::star_count(171, 2.f);
    check(n0 == 512 && n65 == 3120 && n143 == 6864 && n255 == 12240 && n65q == 780 && n65x4 == 12480 && n65bad == 6240 &&
              n65d == 6240 && n255d == 16384 && n171x2 == 16384,
          "star_count");
    check(core::vertex_capacity == 65536 && core::stars_max * 4u - 1u == 65535u, "indices_16bit");
    float density = 1.f, setting = 0.f;
    check(!core::accept_density(0.1f, &density) && density == 1.f && core::accept_density(2.f, &density) && density == 2.f &&
              !core::accept_density(NAN, &density),
          "accept_density");
    setting = core::brightness_default;
    check(!core::accept_brightness(.25f, &setting) && !core::accept_brightness(9.f, &setting) &&
              !core::accept_brightness(NAN, &setting) && setting == 1.5f && core::accept_brightness(8.f, &setting) &&
              setting == 8.f,
          "accept_brightness");
    setting = core::clustering_default;
    check(!core::accept_clustering(-.1f, &setting) && !core::accept_clustering(2.5f, &setting) &&
              !core::accept_clustering(NAN, &setting) && setting == core::clustering_default && core::accept_clustering(0.f, &setting) &&
              setting == 0.f && core::accept_clustering(2.f, &setting) && setting == 2.f,
          "accept_clustering");
    // The seed fold: sector key 0 is the mark seed itself; a key gives seed64 = mark ^ key x 0x9e3779b97f4a7c15 folded.
    check(core::generator_seed(0x014101a7u, 0) == 0x014101a7u && core::generator_seed(core::seed_valid, 0) == core::seed_valid &&
              core::generator_seed(0xffffffffu, 0) == 0xffffffffu && core::generator_seed(0x014101a7u, 1) == 0xe03c040bu &&
              core::generator_seed(0x014101a7u, 0x123456789abcdef0ull) == 0x66a1f36eu,
          "generator_seed_fold");
    {
        // The pass's cached-build check: the field is the same only when mark seed, look and sector key all are.
        const core::Look base{};
        core::Look brighter = base;
        brighter.brightness = 2.f;
        check(core::same_build(0x014101a7u, base, 0, 0x014101a7u, base, 0) &&
                  core::same_build(0x014101a7u, base, 0x20004, 0x014101a7u, base, 0x20004) &&
                  !core::same_build(0x014101a7u, base, 0x20004, 0x014101a7u, base, 0) &&
                  !core::same_build(0x014101a7u, base, 0, 0x014101a7u, base, 0x20004) &&
                  !core::same_build(0x014101a7u, base, 0x20004, 0x014101a7u, base, 0x30004) &&
                  !core::same_build(0x014101a8u, base, 0x20004, 0x014101a7u, base, 0x20004) &&
                  !core::same_build(0x014101a7u, brighter, 0x20004, 0x014101a7u, base, 0x20004),
              "same_build_includes_sector_key");
    }
    setting = core::size_default;
    check(!core::accept_size(-.1f, &setting) && !core::accept_size(3.5f, &setting) && setting == 1.f &&
              core::accept_size(0.f, &setting) && setting == 0.f,
          "accept_size");
    setting = core::min_size_default;
    check(!core::accept_min_size(.5f, &setting) && !core::accept_min_size(3.1f, &setting) && setting == 1.5f &&
              core::accept_min_size(1.f, &setting) && setting == 1.f,
          "accept_min_size");
    setting = core::twinkle_default;
    check(!core::accept_twinkle(-.1f, &setting) && !core::accept_twinkle(1.5f, &setting) && setting == .3f &&
              core::accept_twinkle(0.f, &setting) && setting == 0.f,
          "accept_twinkle");
    // ---- 3. Builder at the defaults: count, stride, unit directions, quad corners, peak cap, tint, law, determinism.
    const std::uint32_t seed = 0xa7u | 1u << 8 | 65u << 16 | core::seed_valid;
    const core::Look look{}; // density 2, brightness 1.5, size 1, min_size 1.5, clustering 1
    core::Look uniform_look = look;
    uniform_look.clustering = 0.f;
    std::vector<core::Vertex> vb(core::vertex_capacity), again(core::vertex_capacity), flat_vb(core::vertex_capacity);
    core::build(seed, uniform_look, flat_vb.data(), core::stars_max);
    core::BuildStats stats;
    const unsigned n = core::build(seed, look, vb.data(), core::stars_max, &stats);
    check(n == 6240 && stats.stars == n, "build_count");
    check(sizeof(core::Vertex) == 32 && core::vertex_stride == 32, "stride_32");
    check(core::build(seed, look, again.data(), core::stars_max) == n &&
              !std::memcmp(vb.data(), again.data(), n * 4 * sizeof(core::Vertex)),
          "deterministic");
    const std::uint32_t seed2 = 0xa8u | 1u << 8 | 65u << 16 | core::seed_valid;
    check(core::build(seed2, look, again.data(), core::stars_max) == n &&
              std::memcmp(vb.data(), again.data(), n * 4 * sizeof(core::Vertex)),
          "seeds_differ");
    check(!core::build(0, look, again.data(), core::stars_max) && !core::build(seed, look, again.data(), 100), "refusals");
    check(core::build(seed, look, again.data(), core::stars_max, nullptr, 0) == n &&
              !std::memcmp(vb.data(), again.data(), n * 4 * sizeof(core::Vertex)) &&
              core::build(seed, look, again.data(), core::stars_max, nullptr, 1) == n &&
              std::memcmp(vb.data(), again.data(), n * 4 * sizeof(core::Vertex)),
          "sector_key_0_is_the_mark_field");
    double peak_max = 0, unit_err = 0, hemi = 0, a_min = 1e9, a_max = 0;
    unsigned bins[4] = {0, 0, 0, 0}, tint_ok = 0, corners_ok = 0;
    std::vector<double> decoded(n), target(n), support(n);
    for (unsigned i = 0; i < n; ++i) {
        const auto& v = vb[i * 4];
        const double len = std::sqrt(double(v.dir[0]) * v.dir[0] + double(v.dir[1]) * v.dir[1] + double(v.dir[2]) * v.dir[2]);
        unit_err = std::fmax(unit_err, std::fabs(len - 1.));
        hemi += flat_vb[i * 4].dir[2] > 0; // the uniform field (clustering 0)
        const double h = std::fabs(v.corner[0]), a = h - core::quad_margin;
        support[i] = a;
        a_min = std::fmin(a_min, a);
        a_max = std::fmax(a_max, a);
        const double peak = v.energy * core::centre_fraction(a); // gain x target peak (no cap growth at the defaults)
        peak_max = std::fmax(peak_max, peak);
        target[i] = peak / double(look.brightness);
        const double lin = std::pow(target[i], core::decode_gamma);
        decoded[i] = lin;
        for (unsigned j = 0; j < 4; ++j)
            if (lin <= std::pow(2.5, -double(j)) + 1e-6 && lin > std::pow(2.5, -double(j + 1))) ++bins[j];
        const unsigned rr = (v.tint >> 16) & 255, gg = (v.tint >> 8) & 255, bb = v.tint & 255;
        tint_ok += (v.tint >> 24) == 255 && (rr == 255 || gg == 255 || bb == 255);
        bool c = true;
        for (unsigned k = 0; k < 4; ++k) {
            const auto& q = vb[i * 4 + k];
            c = c && std::fabs(q.corner[0]) == h && std::fabs(q.corner[1]) == h && q.energy == v.energy && q.tint == v.tint &&
                q.twinkle == v.twinkle && !std::memcmp(q.dir, v.dir, sizeof v.dir);
        }
        corners_ok += c && vb[i * 4].corner[0] < 0 && vb[i * 4 + 3].corner[0] > 0 && vb[i * 4 + 3].corner[1] > 0;
    }
    std::vector<double> sorted = decoded;
    std::sort(sorted.begin(), sorted.end());
    const double median_decoded = sorted[n / 2];
    check(unit_err < 1e-5, "unit_directions");
    check(hemi > n * .46 && hemi < n * .54, "sphere_uniform_hemisphere");
    // At gain 1.5 the brightest star (target peak 1.0) keeps its centred peak at 1.5, under peak_cap: no cap growth.
    check(peak_max <= core::peak_cap + 1e-6 && peak_max <= core::brightness_default + 1e-6 &&
              peak_max > core::brightness_default * .98 && std::fabs(stats.peak_max - peak_max) < 1e-5,
          "peak_cap");
    check(tint_ok == n && corners_ok == n, "tint_and_corners");
    const double r1 = double(bins[1]) / bins[0], r2 = double(bins[2]) / bins[1], r3 = double(bins[3]) / bins[2];
    check(std::fabs(r1 - 2.5) < .45 && std::fabs(r2 - 2.5) < .3 && std::fabs(r3 - 2.5) < .25, "power_law_2_5");
    check(std::fabs(median_decoded - .05) < .006, "median_decoded_0_05");
    // The defaults' footprints: 1.5 px for the dimmest star, 1.5 + 1.4 on the 1/16 grid (2.875) for the brightest.
    check(a_min == 1.5 && a_max == 2.875 && stats.support_max == 2.875, "support_range_defaults");
    // ---- 3b. Size monotonic in brightness: sorted by target peak, the support never shrinks; size 0 = one size.
    double hot_support_max = 0;
    {
        std::vector<unsigned> order(n);
        for (unsigned i = 0; i < n; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](unsigned x, unsigned y) { return target[x] < target[y]; });
        unsigned drops = 0;
        for (unsigned i = 1; i < n; ++i) drops += support[order[i]] < support[order[i - 1]];
        check(!drops, "size_monotonic_in_brightness");
        core::Look flat = look;
        flat.size = 0.f;
        core::build(seed, flat, again.data(), core::stars_max);
        bool one = true;
        for (unsigned i = 0; i < n; ++i) one = one && std::fabs(again[i * 4].corner[0]) == 1.5f + core::quad_margin;
        check(one, "size0_one_footprint");
        // Brightness 8, size 0, min_size 1: the cap search grows the brightest stars; the centred peak stays <= 2.
        core::Look hot{2.f, 8.f, 0.f, 1.f};
        core::BuildStats hs;
        core::build(seed, hot, again.data(), core::stars_max, &hs);
        double hot_peak = 0;
        for (unsigned i = 0; i < n; ++i) {
            const double a = std::fabs(again[i * 4].corner[0]) - core::quad_margin;
            hot_peak = std::fmax(hot_peak, again[i * 4].energy * core::centre_fraction(a));
        }
        check(hot_peak <= core::peak_cap + 1e-6 && hs.support_max > 2. && hs.support_max < core::half_support_max,
              "cap_search_at_gain8");
        hot_support_max = hs.support_max;
    }
    // ---- 3b'. The cap at the range corners: every combination of brightness, size and min_size at their limits (density
    // 4): the centred peak of every star <= peak_cap (the support bound lowers the energy), a <= half_support_max.
    double corner_peak = 0, corner_support = 0;
    unsigned corners_bounded = 0;
    for (float gain : {core::brightness_min, core::brightness_max})
        for (float size : {core::size_min, core::size_max})
            for (float min_size : {core::min_size_min, core::min_size_max}) {
                const core::Look corner{core::density_max, gain, size, min_size};
                core::BuildStats cs;
                const unsigned m = core::build(seed, corner, again.data(), core::stars_max, &cs);
                double worst = 0;
                for (unsigned i = 0; i < m; ++i) {
                    const double a = std::fabs(again[i * 4].corner[0]) - core::quad_margin;
                    worst = std::fmax(worst, again[i * 4].energy * core::centre_fraction(a));
                    corner_support = std::fmax(corner_support, a);
                    corners_bounded += a >= core::half_support_max;
                }
                corner_peak = std::fmax(corner_peak, std::fmax(worst, cs.peak_max));
            }
    check(corner_peak <= core::peak_cap * (1. + 1e-6) && corner_support <= core::half_support_max, "peak_cap_corners");
    // ---- 3c. min_size 1, size 0, gain 1, clustering 0 reproduces the first law's vertices (the 28-byte layout of
    // 2026-11-05) and its directions byte for byte; the field (tint, twinkle) is the same in every look, the directions
    // in every look of the same clustering.
    unsigned legacy_mismatch = 0, field_mismatch = 0;
    {
        const core::Look legacy{1.f, 1.f, 0.f, 1.f, 0.f};
        std::vector<core::Vertex> old(core::vertex_capacity);
        const unsigned m = core::build(seed, legacy, old.data(), core::stars_max);
        // The first law, verbatim: energy = peak / centre_fraction(1), a = 1 (its cap 1.0 never grew a).
        core::Rng rng(seed);
        double class_tint[3];
        core::lane_tint(core::seed_lane(seed), class_tint);
        for (unsigned i = 0; i < m; ++i) {
            double x, y, s;
            do {
                rng.uniform(&x);
                rng.uniform(&y);
                x = 2. * x - 1.;
                y = 2. * y - 1.;
                s = x * x + y * y;
            } while (!(s > 1e-12 && s < 1.));
            const double r = double(x3m::scalar::sqrt(float(1. - s)));
            const float dir[3] = {float(2. * x * r), float(2. * y * r), float(1. - 2. * s)};
            double u;
            rng.uniform(&u);
            const double dec = 1. / (1. + u * (double(core::x_max) - 1.));
            const double peak = core::exp2_bounded(core::log2_positive(dec) / core::decode_gamma);
            const float energy = float(peak / core::centre_fraction(1.));
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
            const float h = 1.f + core::quad_margin;
            const float corners[4][2] = {{-h, -h}, {h, -h}, {-h, h}, {h, h}};
            for (unsigned v = 0; v < 4; ++v) {
                const auto& o = old[i * 4 + v];
                legacy_mismatch += std::memcmp(o.dir, dir, sizeof dir) != 0 || o.corner[0] != corners[v][0] ||
                                   o.corner[1] != corners[v][1] || o.energy != energy || o.tint != tint;
            }
        }
        legacy_mismatch += m != 3120;
        for (unsigned i = 0; i < m; ++i)
            field_mismatch += std::memcmp(old[i * 4].dir, flat_vb[i * 4].dir, sizeof vb[i * 4].dir) != 0 ||
                              old[i * 4].tint != vb[i * 4].tint || old[i * 4].twinkle != vb[i * 4].twinkle;
    }
    check(!legacy_mismatch, "legacy_look_reproduces_first_law");
    check(!field_mismatch, "field_independent_of_look");
    // ---- 3d. Twinkle lanes: about one star in four, chosen independently of brightness; rates and phases in range.
    unsigned twinkling = 0, bad_lanes = 0, bright_quarter = 0, bright_twinkling = 0;
    unsigned rate_lo = 255, rate_hi = 0;
    {
        std::vector<double> t = target;
        std::sort(t.begin(), t.end());
        const double q3 = t[n * 3 / 4];
        for (unsigned i = 0; i < n; ++i) {
            const std::uint32_t w = vb[i * 4].twinkle;
            const bool on = w != 0;
            twinkling += on;
            if (target[i] >= q3) {
                ++bright_quarter;
                bright_twinkling += on;
            }
            if (!on) continue;
            const unsigned rate = (w >> 16) & 255, blue = w & 255, alpha = w >> 24;
            rate_lo = std::min(rate_lo, rate);
            rate_hi = std::max(rate_hi, rate);
            bad_lanes += rate < core::twinkle_rate_min || rate > core::twinkle_rate_max || blue != 255 || alpha != 0;
        }
    }
    const double twinkle_share = double(twinkling) / n, bright_share = double(bright_twinkling) / bright_quarter;
    check(twinkle_share > .22 && twinkle_share < .28 && twinkling == stats.twinkling, "twinkle_quarter");
    check(bright_share > .19 && bright_share < .31, "twinkle_not_by_brightness");
    check(!bad_lanes && rate_lo <= 56 && rate_hi >= 188, "twinkle_lanes");
    // c251: wall seconds modulo 128 and the amplitude; refusals leave (0, 0).
    float tc[4];
    const std::uint64_t f10 = 10000000ull;
    const bool tc_ok = core::twinkle_constants(f10 * (128ull * 5 + 3) + f10 / 4, f10, .3f, tc) &&
                       std::fabs(tc[0] - 3.25f) < 1e-6f && tc[1] == .3f && tc[2] == 0.f && tc[3] == 0.f;
    const std::uint64_t f3g = 2992000000ull, late = (std::uint64_t(1) << 62) + 12345;
    const bool tc_big = core::twinkle_constants(late, f3g, 1.f, tc) && tc[0] >= 0.f && tc[0] < 128.f &&
                        std::fabs(double(tc[0]) - double(late % (f3g * 128)) / double(f3g)) < 1e-4;
    const bool tc_refused = !core::twinkle_constants(5, 0, .3f, tc) && tc[0] == 0.f && tc[1] == 0.f &&
                            !core::twinkle_constants(5, f10, 1.5f, tc) && tc[1] == 0.f;
    check(tc_ok && tc_big && tc_refused, "twinkle_constants");
    // The 128 s wrap is seamless: every rate n / 128 Hz completes n whole cycles in 128 s.
    check(core::twinkle_period_s == 128 && 128 % 128 == 0, "twinkle_period");
    // CRT-free log2/exp2 against <cmath> over the generator's range.
    double pow_err = 0;
    for (double x = 1. / 39.0625; x <= 1.; x *= 1.01)
        pow_err = std::fmax(pow_err, std::fabs(core::exp2_bounded(core::log2_positive(x) / 2.2) - std::pow(x, 1. / 2.2)));
    check(pow_err < 1e-9, "pow_without_crt");
    // ---- 3e. Clustering: 48 equal-area bins (6 bands of equal z x 8 longitude sectors of 45 degrees, binned here by
    // atan2, independently of core::sphere_bin) over 16 seeds of 16384 stars. Clustering 0: near uniform (chi-square of 47
    // degrees of freedom); 1: fullest / emptiest bin >= 4 for every seed and no empty bin; 2 reported. Clustering moves
    // the directions only. Host rebuild times are reported, not asserted (the fixture's COST row is the device figure).
    unsigned only_dirs_differ = 0;
    for (unsigned i = 0; i < n; ++i)
        for (unsigned k = 0; k < 4; ++k) {
            const auto &p = vb[i * 4 + k], &q = flat_vb[i * 4 + k];
            only_dirs_differ += p.energy != q.energy || p.tint != q.tint || p.twinkle != q.twinkle ||
                                p.corner[0] != q.corner[0] || p.corner[1] != q.corner[1];
        }
    check(!only_dirs_differ, "clustering_moves_directions_only");
    struct ClusterRun {
        double ratio_min = 1e30, ratio_max = 0, chi2_max = 0, reject_sum = 0, build_us = 1e30;
        unsigned bin_min = ~0u, exhausted = 0, stat_mismatch = 0;
    };
    auto cluster_run = [&](float clustering) {
        ClusterRun run;
        core::Look l = look;
        l.clustering = clustering;
        for (unsigned s = 0; s < 16; ++s) {
            const std::uint32_t sd = (0x10u + 37u * s) % 256u | (s % 7u) << 8 | 255u << 16 | core::seed_valid;
            core::BuildStats st;
            const auto t0 = std::chrono::steady_clock::now();
            const unsigned m = core::build(sd, l, again.data(), core::stars_max, &st);
            const auto t1 = std::chrono::steady_clock::now();
            run.build_us = std::fmin(run.build_us, std::chrono::duration<double, std::micro>(t1 - t0).count());
            unsigned bin[core::sphere_bins] = {};
            for (unsigned i = 0; i < m; ++i) {
                const float* d = again[i * 4].dir;
                int band = int((double(d[2]) + 1.) * 3.);
                band = band < 0 ? 0 : band > 5 ? 5 : band;
                double phi = std::atan2(double(d[1]), double(d[0]));
                if (phi < 0) phi += 2. * 3.14159265358979323846;
                int sector = int(phi / (3.14159265358979323846 / 4.));
                sector = sector > 7 ? 7 : sector;
                ++bin[band * 8 + sector];
            }
            const unsigned lo = *std::min_element(bin, bin + core::sphere_bins);
            const unsigned hi = *std::max_element(bin, bin + core::sphere_bins);
            const double expect = double(m) / core::sphere_bins;
            double chi2 = 0;
            for (unsigned b : bin) chi2 += (b - expect) * (b - expect) / expect;
            const double ratio = double(hi) / double(lo ? lo : 1u);
            run.stat_mismatch += st.bin_min != lo || st.bin_max != hi || std::fabs(st.cluster_max_min - ratio) > 1e-12 ||
                                 m != core::stars_max;
            run.ratio_min = std::fmin(run.ratio_min, ratio);
            run.ratio_max = std::fmax(run.ratio_max, ratio);
            run.chi2_max = std::fmax(run.chi2_max, chi2);
            run.bin_min = std::min(run.bin_min, lo);
            run.reject_sum += st.reject_rate;
            run.exhausted += st.exhausted;
        }
        return run;
    };
    const ClusterRun c0 = cluster_run(0.f), c1 = cluster_run(1.f), c2 = cluster_run(2.f);
    check(!c0.stat_mismatch && !c1.stat_mismatch && !c2.stat_mismatch, "cluster_stats_match_independent_bins");
    // 47 degrees of freedom: mean 47, sd 9.7; 47 + 5 sd = 95.5 bounds the worst of 16 seeds.
    check(c0.chi2_max < 95.5 && c0.reject_sum == 0. && !c0.exhausted, "clustering0_uniform_chi2");
    check(c1.ratio_min >= 4. && c1.bin_min > 0, "clustering1_max_min_ge_4");
    check(c2.ratio_min >= c1.ratio_min && c2.bin_min > 0, "clustering2_stronger");
    // ---- 4. Kernel: the covered pixels sum to the energy at any sub-pixel offset, peak <= peak_cap, nothing outside.
    double energy_err = 0, splat_peak = 0;
    const double offsets[6][2] = {{0, 0}, {.25, .5}, {.5, .5}, {.37, .81}, {.999, .001}, {.5, 0}};
    unsigned outside = 0;
    for (unsigned i = 0; i < n; i += 7)
        for (const auto& o : offsets) {
            const auto& v = vb[i * 4];
            const Splat s = splat(v, 100. + o[0], 100. + o[1], 0, 0, nullptr);
            const double expect = v.energy * double((v.tint >> 16) & 255) / 255.;
            energy_err = std::fmax(energy_err, std::fabs(s.sum[0] - expect) / expect);
            splat_peak = std::fmax(splat_peak, s.peak);
            // Weight beyond the quad would be lost: a pixel just outside carries none.
            const double h = std::fabs(v.corner[0]), a = h - core::quad_margin;
            for (int x = s.x0 - 2; x <= s.x1 + 2; ++x)
                if ((x < s.x0 || x > s.x1) && core::axis_weight(double(x) - 100. - o[0], a) > 0) ++outside;
        }
    check(energy_err < 1e-9, "energy_invariant");
    check(splat_peak <= core::peak_cap + 1e-6, "splat_peak_cap");
    check(!outside, "nothing_outside_quad");
    // ---- 5. Meter replay (invariant 5): star-only frames at 5120x1440, and the guard's control (one luminance-64 pixel
    // per 64x64 tile on black).
    double peak_a = 0, peak_b = 0;
    const Meter a = star_frame(seed, look, 5120, 1440, &peak_a);
    const std::uint32_t dense = 0x11u | 0u << 8 | 143u << 16 | core::seed_valid;
    const core::Look dense_look{4.f, core::brightness_default, core::size_default, core::min_size_default};
    const Meter b = star_frame(dense, dense_look, 5120, 1440, &peak_b);
    // Every member at its maximum: reported, not asserted (a user's choice may move the exposure; the row says how far).
    double peak_x = 0;
    const Meter x = star_frame(dense, core::Look{4.f, 8.f, 3.f, 3.f}, 5120, 1440, &peak_x);
    std::vector<float> control(std::size_t(5120) * 1440 * 3, 0.f);
    const float engine64 = float(std::pow(64., 1. / 2.2));
    for (unsigned y = 32; y < 1440; y += 64)
        for (unsigned x = 32; x < 5120; x += 64)
            for (unsigned c = 0; c < 3; ++c) control[(std::size_t(y) * 5120 + x) * 3 + c] = engine64;
    const Meter g = meter(control, 5120, 1440, 0);
    check(a.neutral && a.ev_target == 0. && b.neutral && b.ev_target == 0., "meter_star_frames_ev0");
    check(a.ev_limit >= 1.3 && b.ev_limit >= 1.3, "meter_guard_unmoved");
    check(g.ev_limit < -2.0, "meter_guard_control");
    std::printf("{\"ok\": %s, \"checks\": %u, \"failures\": %u, \"failed\": \"%s\",\n", failures ? "false" : "true", checks,
                failures, failed.c_str());
    std::printf(" \"star_count\": {\"k0\": %u, \"k65\": %u, \"k143\": %u, \"k255\": %u, \"k65_d0.25\": %u, \"k65_d4\": %u,"
                " \"k65_default\": %u, \"k255_default\": %u},\n",
                n0, n65, n143, n255, n65q, n65x4, n65d, n255d);
    std::printf(" \"build\": {\"seed\": \"%08x\", \"stars\": %u, \"bytes\": %u, \"peak_max\": %.6f, \"median_decoded\": %.5f,"
                " \"bins_decoded\": [%u, %u, %u, %u], \"bin_ratios\": [%.3f, %.3f, %.3f], \"unit_err\": %.2e,"
                " \"support_min\": %.4f, \"support_max\": %.4f, \"hot_support_max\": %.4f, \"pow_err\": %.2e},\n",
                seed, n, n * 4 * unsigned(sizeof(core::Vertex)), peak_max, median_decoded, bins[0], bins[1], bins[2], bins[3],
                r1, r2, r3, unit_err, a_min, a_max, hot_support_max, pow_err);
    std::printf(" \"corners\": {\"peak_max\": %.6f, \"support_max\": %.4f, \"stars_at_bound\": %u},\n", corner_peak,
                corner_support, corners_bounded);
    std::printf(" \"legacy\": {\"mismatches\": %u, \"field_mismatches\": %u},\n", legacy_mismatch, field_mismatch);
    auto cluster_json = [](const char* name, const ClusterRun& c) {
        std::printf(" \"%s\": {\"seeds\": 16, \"stars\": %u, \"ratio_min\": %.3f, \"ratio_max\": %.3f, \"bin_min\": %u,"
                    " \"chi2_max\": %.2f, \"reject_rate_mean\": %.4f, \"exhausted\": %u, \"host_build_us_best\": %.0f},\n",
                    name, core::stars_max, c.ratio_min, c.ratio_max, c.bin_min, c.chi2_max, c.reject_sum / 16., c.exhausted,
                    c.build_us);
    };
    cluster_json("clustering0", c0);
    cluster_json("clustering1", c1);
    cluster_json("clustering2", c2);
    std::printf(" \"clustering_default_build\": {\"cluster_max_min\": %.3f, \"reject_rate\": %.4f, \"only_dirs_differ\": %u},\n",
                stats.cluster_max_min, stats.reject_rate, only_dirs_differ);
    std::printf(" \"twinkle\": {\"stars\": %u, \"share\": %.4f, \"bright_quarter_share\": %.4f, \"rate_min\": %u,"
                " \"rate_max\": %u, \"bad_lanes\": %u},\n",
                twinkling, twinkle_share, bright_share, rate_lo, rate_hi, bad_lanes);
    std::printf(" \"kernel\": {\"energy_rel_err\": %.2e, \"peak_max\": %.6f, \"outside\": %u, \"centre_fraction_a1\": %.6f},\n",
                energy_err, splat_peak, outside, core::centre_fraction(1.));
    auto meter_json = [](const char* name, const Meter& m, double peak, const char* tail) {
        std::printf(" \"%s\": {\"ev_target\": %.4f, \"ev_key\": %.4f, \"ev_limit\": %.4f, \"p99_max_log\": %.4f, \"neutral\": %s,"
                    " \"lit\": %u, \"tiles\": %u, \"tile_image\": \"%ux%u\", \"visible_stars\": %u, \"frame_max\": %.5f,"
                    " \"star_peak\": %.5f}%s\n",
                    name, m.ev_target, m.ev_key, m.ev_limit, m.p99_max_log, m.neutral ? "true" : "false", m.lit, m.tiles,
                    m.tile_w, m.tile_h, m.visible, m.frame_max, peak, tail);
    };
    meter_json("meter_k65_defaults", a, peak_a, ",");
    meter_json("meter_k143_d4", b, peak_b, ",");
    meter_json("meter_k143_max_look", x, peak_x, ",");
    meter_json("meter_control_lum64", g, 0., "}");
    return failures ? 1 : 0;
}
