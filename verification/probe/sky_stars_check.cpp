// Host check of src/proxy/sky_stars_core.h with src/proxy/lod_texture_mark.h and the installed exposure meter
// (src/renderer/exposure.cpp) for verification/analysis/test_sky_stars.py. Prints one JSON object: the mark parse of a
// payload-2 descriptor and the table round trip of K, the seed and N derivation, the vertex-buffer builder (count, stride,
// magnitude law, peak cap, determinism), the kernel's energy at sub-pixel offsets, and the meter replay on star-only
// frames (invariant 5 of docs/architecture/sector-background-modern.md). No D3D, no Windows.
#include "../../src/proxy/sky_stars_core.h"
#include "../../src/renderer/exposure.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
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
static Meter star_frame(std::uint32_t seed, float density, unsigned width, unsigned height, double* worst_peak) {
    std::vector<core::Vertex> vb(core::vertex_capacity);
    const unsigned n = core::build(seed, density, vb.data(), core::stars_max);
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
    // ---- 2. N = clamp(round(K x 48 x density), 512, 8192).
    const unsigned n0 = core::star_count(0, 1.f), n65 = core::star_count(65, 1.f), n143 = core::star_count(143, 1.f),
                   n255 = core::star_count(255, 1.f), n65q = core::star_count(65, .25f), n65x4 = core::star_count(65, 4.f),
                   n65bad = core::star_count(65, 9.f);
    check(n0 == 512 && n65 == 3120 && n143 == 6864 && n255 == 8192 && n65q == 780 && n65x4 == 8192 && n65bad == 3120,
          "star_count");
    float density = 1.f;
    check(!core::accept_density(0.1f, &density) && density == 1.f && core::accept_density(2.f, &density) && density == 2.f &&
              !core::accept_density(NAN, &density),
          "accept_density");
    // ---- 3. Builder: count, stride, unit directions, quad corners, peak cap, tint, magnitude law, determinism.
    const std::uint32_t seed = 0xa7u | 1u << 8 | 65u << 16 | core::seed_valid;
    std::vector<core::Vertex> vb(core::vertex_capacity), again(core::vertex_capacity);
    core::BuildStats stats;
    const unsigned n = core::build(seed, 1.f, vb.data(), core::stars_max, &stats);
    check(n == 3120 && stats.stars == n, "build_count");
    check(sizeof(core::Vertex) == 28 && core::vertex_stride == 28, "stride_28");
    check(core::build(seed, 1.f, again.data(), core::stars_max) == n &&
              !std::memcmp(vb.data(), again.data(), n * 4 * sizeof(core::Vertex)),
          "deterministic");
    const std::uint32_t seed2 = 0xa8u | 1u << 8 | 65u << 16 | core::seed_valid;
    check(core::build(seed2, 1.f, again.data(), core::stars_max) == n &&
              std::memcmp(vb.data(), again.data(), n * 4 * sizeof(core::Vertex)),
          "seeds_differ");
    check(!core::build(0, 1.f, again.data(), core::stars_max) && !core::build(seed, 1.f, again.data(), 100), "refusals");
    double peak_max = 0, unit_err = 0, hemi = 0;
    unsigned bins[4] = {0, 0, 0, 0}, tint_ok = 0, corners_ok = 0;
    std::vector<double> decoded(n);
    for (unsigned i = 0; i < n; ++i) {
        const auto& v = vb[i * 4];
        const double len = std::sqrt(double(v.dir[0]) * v.dir[0] + double(v.dir[1]) * v.dir[1] + double(v.dir[2]) * v.dir[2]);
        unit_err = std::fmax(unit_err, std::fabs(len - 1.));
        hemi += v.dir[2] > 0;
        const double h = std::fabs(v.corner[0]), a = h - core::quad_margin;
        const double peak = v.energy * core::centre_fraction(a);
        peak_max = std::fmax(peak_max, peak);
        const double lin = std::pow(peak, core::decode_gamma);
        decoded[i] = lin;
        for (unsigned j = 0; j < 4; ++j)
            if (lin <= std::pow(2.5, -double(j)) + 1e-9 && lin > std::pow(2.5, -double(j + 1))) ++bins[j];
        const unsigned rr = (v.tint >> 16) & 255, gg = (v.tint >> 8) & 255, bb = v.tint & 255;
        tint_ok += (v.tint >> 24) == 255 && (rr == 255 || gg == 255 || bb == 255);
        bool c = true;
        for (unsigned k = 0; k < 4; ++k) {
            const auto& q = vb[i * 4 + k];
            c = c && std::fabs(q.corner[0]) == h && std::fabs(q.corner[1]) == h && q.energy == v.energy && q.tint == v.tint &&
                !std::memcmp(q.dir, v.dir, sizeof v.dir);
        }
        corners_ok += c && vb[i * 4].corner[0] < 0 && vb[i * 4 + 3].corner[0] > 0 && vb[i * 4 + 3].corner[1] > 0;
    }
    std::vector<double> sorted = decoded;
    std::sort(sorted.begin(), sorted.end());
    const double median_decoded = sorted[n / 2];
    check(unit_err < 1e-5, "unit_directions");
    check(hemi > n * .46 && hemi < n * .54, "sphere_uniform_hemisphere");
    check(peak_max <= 1. + 1e-6 && peak_max > .98, "peak_cap");
    check(tint_ok == n && corners_ok == n, "tint_and_corners");
    const double r1 = double(bins[1]) / bins[0], r2 = double(bins[2]) / bins[1], r3 = double(bins[3]) / bins[2];
    check(std::fabs(r1 - 2.5) < .45 && std::fabs(r2 - 2.5) < .3 && std::fabs(r3 - 2.5) < .25, "power_law_2_5");
    check(std::fabs(median_decoded - .05) < .006, "median_decoded_0_05");
    // CRT-free log2/exp2 against <cmath> over the generator's range.
    double pow_err = 0;
    for (double x = 1. / 39.0625; x <= 1.; x *= 1.01)
        pow_err = std::fmax(pow_err, std::fabs(core::exp2_bounded(core::log2_positive(x) / 2.2) - std::pow(x, 1. / 2.2)));
    check(pow_err < 1e-9, "pow_without_crt");
    // ---- 4. Kernel: the covered pixels sum to the energy at any sub-pixel offset, peak <= 1, nothing outside the quad.
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
    check(splat_peak <= 1. + 1e-6, "splat_peak_cap");
    check(!outside, "nothing_outside_quad");
    // ---- 5. Meter replay (invariant 5): star-only frames at 5120x1440, and the guard's control (one luminance-64 pixel
    // per 64x64 tile on black).
    double peak_a = 0, peak_b = 0;
    const Meter a = star_frame(seed, 1.f, 5120, 1440, &peak_a);
    const std::uint32_t dense = 0x11u | 0u << 8 | 143u << 16 | core::seed_valid;
    const Meter b = star_frame(dense, 4.f, 5120, 1440, &peak_b);
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
    std::printf(" \"star_count\": {\"k0\": %u, \"k65\": %u, \"k143\": %u, \"k255\": %u, \"k65_d0.25\": %u, \"k65_d4\": %u},\n",
                n0, n65, n143, n255, n65q, n65x4);
    std::printf(" \"build\": {\"seed\": \"%08x\", \"stars\": %u, \"bytes\": %u, \"peak_max\": %.6f, \"median_decoded\": %.5f,"
                " \"bins_decoded\": [%u, %u, %u, %u], \"bin_ratios\": [%.3f, %.3f, %.3f], \"unit_err\": %.2e,"
                " \"support_max\": %.4f, \"pow_err\": %.2e},\n",
                seed, n, n * 4 * 28, peak_max, median_decoded, bins[0], bins[1], bins[2], bins[3], r1, r2, r3, unit_err,
                stats.support_max, pow_err);
    std::printf(" \"kernel\": {\"energy_rel_err\": %.2e, \"peak_max\": %.6f, \"outside\": %u, \"centre_fraction_a1\": %.6f},\n",
                energy_err, splat_peak, outside, core::centre_fraction(1.));
    auto meter_json = [](const char* name, const Meter& m, double peak, const char* tail) {
        std::printf(" \"%s\": {\"ev_target\": %.4f, \"ev_key\": %.4f, \"ev_limit\": %.4f, \"p99_max_log\": %.4f, \"neutral\": %s,"
                    " \"lit\": %u, \"tiles\": %u, \"tile_image\": \"%ux%u\", \"visible_stars\": %u, \"frame_max\": %.5f,"
                    " \"star_peak\": %.5f}%s\n",
                    name, m.ev_target, m.ev_key, m.ev_limit, m.p99_max_log, m.neutral ? "true" : "false", m.lit, m.tiles,
                    m.tile_w, m.tile_h, m.visible, m.frame_max, peak, tail);
    };
    meter_json("meter_k65_d1", a, peak_a, ",");
    meter_json("meter_k143_d4", b, peak_b, ",");
    meter_json("meter_control_lum64", g, 0., "}");
    return failures ? 1 : 0;
}
