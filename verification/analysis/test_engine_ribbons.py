"""Engine ribbons and the plumes' fog, phase 3 (X3M_ENGINE_EFFECTS=plumes; docs/architecture/engine-effects-modern.md
sections 3-5): the portable cores src/proxy/engine_ribbons_core.h and src/renderer/fog_transmittance.h compiled on the
host (the ring buffers: distance sampling, the SETA stretch, the length law, cut / load / jump clears, fade and eviction,
the 256 cap, identity keys, the clock re-base; the strip builder; the fog law: exp, the column, the mean density, the
compiled occupancy table against tools/fog_field_recipe.py, the plume builder's fog), the wiring, and the tracked Wine
record of run_engine_ribbons.py bound to its production sources.
"""
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from source_text import source_text  # noqa: E402

HARNESS = r'''
#include "engine_ribbons_core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::engine_ribbons;
namespace ee = x3m::engine_effects::core;
namespace ep = x3m::engine_plumes;
namespace fr = x3m::renderer;
static unsigned failed = 0, checks = 0;
static void expect(bool ok, const char* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
static bool near(double a, double b, double rel) { return std::fabs(a - b) <= rel * (std::fabs(b) > 1. ? std::fabs(b) : 1.); }
static ep::View view() { ep::View v; const float r[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; std::memcpy(v.rows, r, sizeof r); v.m00 = 1.7f * 1080.f / 1920.f; v.m11 = 1.7f; v.height = 1080.f; v.near_z = 1.f; return v; }
static ee::Record rec(float x, float y, float z, float value, float s, std::uint32_t handle = 7, std::uint64_t serial = 0) {
    ee::Record r{};
    r.origin[0] = x; r.origin[1] = y; r.origin[2] = z; r.axis[0] = -1.f; r.size = value; r.s = s; r.z = .25f + 1.75f * s;
    r.body = -1; r.flags = std::uint16_t((unsigned(ee::white) << ee::cluster_shift) | (serial ? unsigned(ee::flag_serial) : 0u));
    r.serial = serial; r.node_handle = handle; r.model = 20000; return r;
}
static const Ribbon* only(const Pool& p) { for (const auto& r : p.ribbons) if (r.live) return &r; return nullptr; }
// The drawn polyline's length (the builder's along[] at the last point) from a built ribbon: u at the last distinct point is 1.
static double drawn_length(const Ribbon& r, const ep::View& v, double now, const Pool& p) {
    std::vector<Vertex> out(vertices_per_ribbon); BuildStats st{};
    if (!build_ribbon(r, r.seen == p.serial, float(now - p.origin), v, 1.f, out.data(), &st)) return 0.;
    double len = 0.; for (unsigned k = 1; k < points_per_ribbon; ++k) {
        const float* a = out[2 * (k - 1)].position; const float* b = out[2 * k].position;
        const float* a1 = out[2 * (k - 1) + 1].position; const float* b1 = out[2 * k + 1].position;
        double d = 0; for (int j = 0; j < 3; ++j) { const double c = .5 * (b[j] + b1[j]) - .5 * (a[j] + a1[j]); d += c * c; } len += std::sqrt(d); }
    return len;
}
int main() {
    const float dt = 1.f / 60.f;
    // ------------------------------------------------------------------ fog: exp, column, mean density, law
    {
        double worst = 0; for (float x = 0.f; x <= 87.f; x += .0137f) { float e; fr::fog_exp_negative(x, &e); const double ref = std::exp(-double(x)); worst = std::max(worst, std::fabs(e - ref) / ref); }
        float e0, e1, e2; fr::fog_exp_negative(0.f, &e0); fr::fog_exp_negative(-3.f, &e1); fr::fog_exp_negative(100.f, &e2);
        std::printf("FOG exp_rel_error=%.3g\n", worst);
        expect(worst < 1e-5 && e0 == 1.f && e1 == 1.f && e2 == 0.f, "exp(-x): rel error < 1e-5 on [0, 87], 1 at x <= 0, 0 past 87");
        float d; const float S = fr::fog_transmittance_start, C = fr::fog_transmittance_cap;
        fr::fog_column_depth(1000.f, S, C, &d); expect(d == 1000.f, "column: the distance before the taper");
        fr::fog_column_depth(S, S, C, &d); expect(near(d, S, 1e-6), "column: start");
        fr::fog_column_depth(C, S, C, &d); expect(near(d, S + .5 * (C - S), 1e-6), "column: the smoothstep fade integrates to half its band");
        float far; fr::fog_column_depth(10.f * C, S, C, &far); expect(far == d, "column: nothing past the cap");
        // numeric integral of the weight 1 - smoothstep
        double num = 0; const int N = 200000; for (int i = 0; i < N; ++i) { const double x = (i + .5) / N * 90000.; double w = 1; if (x > S) { double u = std::min(1., (x - S) / (C - S)); w = 1 - u * u * (3 - 2 * u); } num += w * 90000. / N; }
        fr::fog_column_depth(90000.f, S, C, &d); expect(near(d, num, 1e-5), "column: matches the integral of the march's taper");
        float m12, m24; fr::fog_mean_density(.12f, &m12); fr::fog_mean_density(.24f, &m24);
        expect(near(m12, .05613, 1e-5) && near(m24, .10980, 1e-5), "mean density through the measured points");
        std::printf("OCCUPANCY"); for (unsigned id = 0; id <= 15; ++id) std::printf(" %u:%.2f", id, double(fr::fog_compiled_occupancy(id))); std::printf("\n");
        fr::FogTransmittanceLaw law; const float chroma[3] = {1.f, .5f, 0.f};
        fr::fog_transmittance_law(0.f, .12f, chroma, .6f, &law); expect(!law.on, "law: zero extinction is off");
        const float sigma_eff = 2.5e-6f * 1.f * 1.f * 8.f; // bluewell at the 1.0x look, ready
        fr::fog_transmittance_law(sigma_eff, .12f, chroma, .6f, &law);
        expect(law.on && near(law.extinction, sigma_eff * .05613, 1e-5) && near(law.k[0], 1., 1e-6) && near(law.k[1], 1.3, 1e-6) && near(law.k[2], 1.6, 1e-6), "law: extinction and the tinted exponents");
        float t[3]; fr::fog_transmittance(law, 20000.f, t);
        expect(near(t[0], std::exp(-law.extinction * 20000.), 1e-5) && near(t[2], std::exp(-1.6 * law.extinction * 20000.), 1e-5), "transmittance at 20 km");
        float tc[3]; fr::fog_transmittance(law, C, tc);
        std::printf("FOG bluewell_T_4km=%.4f T_cap=%.4f\n", double(t[0]), double(tc[0]));
        fr::FogTransmittanceLaw off; float o[3]; fr::fog_transmittance(off, 50000.f, o); expect(o[0] == 1.f && o[1] == 1.f && o[2] == 1.f, "off: T = 1");
        // the plume builder takes the same factor per channel (View::fog)
        ep::View v = view(); v.fog = law; v.fog.extinction *= 20.f; // a dense column: a clear attenuation
        ee::Record r = rec(0, 0, 30000.f, 400.f, 1.f); ep::Vertex pv[8]; ep::BuildStats bs{};
        const unsigned n = ep::build(&r, 1, nullptr, v, ep::Preset::standard, 3, pv, 1, &bs);
        float want[3]; fr::fog_transmittance(v.fog, 30000.f, want);
        const unsigned red = (pv[0].tint >> 16) & 255u, green = (pv[0].tint >> 8) & 255u, blue = pv[0].tint & 255u;
        std::printf("PLUME_FOG T=%.4f,%.4f,%.4f tint=%u,%u,%u fogged=%u fog_min=%.4f\n", double(want[0]), double(want[1]), double(want[2]), red, green, blue, bs.fogged, double(bs.fog_min));
        expect(n == 1 && bs.fogged == 1 && std::abs(int(red) - int(want[0] * 255.f + .5f)) <= 1 && std::abs(int(blue) - int(want[2] * 255.f + .5f)) <= 1 && near(bs.fog_min, want[2], 1e-5) && pv[0].fog == ep::pack_colour(want) && pv[4].fog == pv[0].fog, "plume builder: tint x T_rgb at the nozzle's distance, T_rgb for the white-hot core");
        ep::View clear = view(); ep::build(&r, 1, nullptr, clear, ep::Preset::standard, 3, pv, 1, &bs);
        expect(pv[0].tint == 0xffffffffu && pv[0].fog == 0xffffffffu && bs.fogged == 0, "plume builder: no fog without the law");
    }
    const ep::View v = view();
    // ------------------------------------------------------------------ distance sampling and the length law
    for (const float value : {1000.f, 5000.f, 30000.f}) {
        static Pool pool; pool.clear(); pool.load_epoch_set = false;
        const float speed = 40.f * value; // units per second: 40 values per second
        double now = 100.0; UpdateStats st{};
        for (unsigned f = 0; f < 240; ++f, now += dt) { const ee::Record r = rec(speed * float(f) * dt, 0, 50000.f, value, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        const Ribbon* r = only(pool);
        float T; trail_seconds(value, &T);
        bool spaced = true; float min_gap = 1e30f;
        for (unsigned b = 0; b + 1 < r->count; ++b) { float d; detail::distance(r->at(b).p, r->at(b + 1).p, &d); min_gap = std::min(min_gap, d); }
        const float floor_spacing = std::max(min_spacing, spacing_value * value);
        spaced = min_gap >= std::max(floor_spacing, r->length / 15.f) * .999f;
        const double drawn = drawn_length(*r, v, now - dt, pool);
        std::printf("LENGTH value=%.0f T=%.1f speed=%.1f v_est=%.1f target=%.1f drawn=%.1f samples=%u min_gap=%.1f\n", double(value), double(T), double(speed), double(r->speed), double(r->length), drawn, r->count, double(min_gap));
        expect(near(r->speed, speed, 1e-3), "v_est: the samples' distance over their age");
        expect(near(r->length, T * speed, 1e-3), "L = T(value) x v x s");
        expect(near(drawn, r->length, 1e-3), "the strip is cut at L");
        expect(r->count == samples_per_ribbon && spaced, "16 samples, spaced at least max(1.5 m, 0.02 value, L / 15)");
    }
    {
        float a, b, c, d; trail_seconds(1999.f, &a); trail_seconds(2000.f, &b); trail_seconds(20000.f, &c); trail_seconds(20001.f, &d);
        expect(a == .5f && b == .8f && c == .8f && d == 1.2f, "T: 0.5 s below 2,000, 0.8 s to 20,000, 1.2 s above");
    }
    // throttle and presets on the length
    for (const float s : {0.f, .5f}) for (const float preset : {.6f, 1.5f}) {
        static Pool pool; pool.clear();
        double now = 5.0; for (unsigned f = 0; f < 120; ++f, now += dt) { const ee::Record r = rec(4000.f * float(f) * dt, 0, 50000.f, 1000.f, s); update(pool, &r, 1, now, false, 0, nullptr, preset, nullptr); }
        expect(near(only(pool)->length, .5 * preset * 4000. * s, 1e-3), "L scales with s and the preset");
    }
    // the travel look under SETA (gap 7 of the gap analysis): T x (1 + travel) at the plumes' travel weight
    for (const float travel : {0.f, .5f, 1.f, 3.f}) {
        static Pool pool; pool.clear();
        double now = 5.0; for (unsigned f = 0; f < 120; ++f, now += dt) { const ee::Record r = rec(4000.f * float(f) * dt, 0, 50000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, nullptr, nullptr, nullptr, nullptr, travel); }
        const double want = .5 * 4000. * (1. + std::min(double(travel), 1.));
        expect(near(only(pool)->length, want, 1e-3), "travel weight 0 / 0.5 / 1 (3 held to 1): T x 1 / 1.5 / 2");
    }
    // ------------------------------------------------------------------ stationary: one sample, no ribbon
    {
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{};
        for (unsigned f = 0; f < 120; ++f, now += dt) { const ee::Record r = rec(10.f, 0, 5000.f, 500.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        std::vector<Vertex> out(vertices_per_ribbon * 4); BuildStats bs{};
        const unsigned drawn = build(pool, v, ep::Preset::standard, now - dt, out.data(), 4, &bs);
        expect(only(pool)->count == 1 && only(pool)->length == 0.f && drawn == 0, "stationary: one sample, no growth, nothing drawn");
    }
    // ------------------------------------------------------------------ SETA: 20x for 10 frames, continuous
    {
        static Pool pool; pool.clear(); double now = 0.0; float x = 0.f; const float value = 800.f, base = 1500.f; UpdateStats st{};
        unsigned jumps = 0;
        for (unsigned f = 0; f < 70; ++f, now += dt) {
            x += (f >= 60 ? 20.f : 1.f) * base * dt; const ee::Record r = rec(x, 0, 60000.f, value, 1.f);
            update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); jumps += st.jumps;
        }
        const Ribbon* r = only(pool);
        float max_gap = 0.f; float d; detail::distance(r->head, r->at(0).p, &d); max_gap = d;
        for (unsigned b = 0; b + 1 < r->count; ++b) { detail::distance(r->at(b).p, r->at(b + 1).p, &d); max_gap = std::max(max_gap, d); }
        const double drawn = drawn_length(*r, v, now - dt, pool);
        std::printf("SETA v_est=%.1f expected=%.1f length=%.1f drawn=%.1f samples=%u max_gap=%.1f jumps=%u\n", double(r->speed), double(20.f * base), double(r->length), drawn, r->count, double(max_gap), jumps);
        expect(jumps == 0 && r->count == samples_per_ribbon, "SETA: no restart, the samples advance");
        // L = T v_est s assumes the new speed for T; the strip ends at the oldest sample, so it covers the path flown in the
        // window: every SETA frame (10 x 20 x base x dt) and the samples before them, without a gap wider than one spacing
        // plus one frame.
        double path = 0; detail::distance(r->head, r->at(0).p, &d); path = d;
        for (unsigned b = 0; b + 1 < r->count; ++b) { detail::distance(r->at(b).p, r->at(b + 1).p, &d); path += d; }
        expect(near(r->speed, 20. * base, .02) && near(drawn, std::min(double(r->length), path), 1e-3) && drawn >= 10. * 20. * base * dt &&
               max_gap <= r->length / 15.f + 20.f * base * dt + 1.f, "SETA: v_est follows the stretch, the strip covers the flown path, no gap wider than one spacing plus one frame");
    }
    // ------------------------------------------------------------------ cut: cleared, no bridge
    {
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{};
        for (unsigned f = 0; f < 60; ++f, now += dt) { const ee::Record r = rec(3000.f * float(f) * dt, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        const ee::Record moved = rec(-50000.f, 0, 40000.f, 1000.f, 1.f);
        update(pool, &moved, 1, now, true, 0, nullptr, 1.f, &st);
        const Ribbon* r = only(pool);
        expect(st.cut_clear && pool.cut_clears == 1 && r && r->count == 1 && r->at(0).p[0] == -50000.f && r->length == 0.f, "cut: cleared, the new ribbon starts at the new position");
        now += dt; const ee::Record next = rec(-50000.f + 3000.f * dt, 0, 40000.f, 1000.f, 1.f); update(pool, &next, 1, now, false, 0, nullptr, 1.f, &st);
        float far = 0; for (unsigned b = 0; b < only(pool)->count; ++b) far = std::max(far, std::fabs(only(pool)->at(b).p[0] + 50000.f));
        expect(far < 100.f, "cut: no sample of the old path survives");
    }
    // ------------------------------------------------------------------ load epoch and jump
    {
        static Pool pool; pool.clear(); pool.load_epoch_set = false; double now = 0.0; UpdateStats st{};
        for (unsigned f = 0; f < 10; ++f, now += dt) { const ee::Record r = rec(3000.f * float(f) * dt, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 5, nullptr, 1.f, &st); }
        const ee::Record r = rec(0, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 6, nullptr, 1.f, &st);
        expect(st.load_clear && pool.load_clears == 1 && only(pool)->count == 1, "load epoch change: cleared");
        update(pool, &r, 1, now + dt, false, 0, nullptr, 1.f, &st); expect(!st.load_clear, "load epoch 0 (unknown): no clear");
        const ee::Record jumped = rec(9000.f, 0, 40000.f, 1000.f, 1.f); update(pool, &jumped, 1, now + 2 * dt, false, 6, nullptr, 1.f, &st);
        expect(st.jumps == 1 && only(pool)->count == 1 && pool.jumps == 1, "a head moved more than 8 value: the ribbon restarts");
    }
    // ------------------------------------------------------------------ identity loss: fade 0.3 s, eviction
    {
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{};
        for (unsigned f = 0; f < 60; ++f, now += dt) { const ee::Record r = rec(3000.f * float(f) * dt, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        std::vector<Vertex> out(vertices_per_ribbon); BuildStats bs{};
        build(pool, v, ep::Preset::standard, now - dt, out.data(), 1, &bs);
        const float full = out[0].shape[0];
        unsigned fading_frames = 0; float half = 0.f; double evicted_at = -1;
        const double lost = now - dt;
        for (unsigned f = 0; f < 40; ++f, now += dt) {
            update(pool, nullptr, 0, now, false, 0, nullptr, 1.f, &st);
            if (st.evicted) { evicted_at = now - lost; break; }
            fading_frames += st.fading;
            build(pool, v, ep::Preset::standard, now, out.data(), 1, &bs);
            if (f == 8) half = out[0].shape[0] / full; // 9 frames = 0.15 s after the last record
        }
        std::printf("FADE full=%.4f at_0.15s=%.4f fading_frames=%u evicted_after=%.4f evictions=%llu\n", double(full), double(half), fading_frames, evicted_at, (unsigned long long)pool.evictions);
        expect(near(half, .5, .02), "fade: linear over 0.3 s (half at 0.15 s)");
        expect(evicted_at >= .3 - 1e-6 && evicted_at < .3 + dt + 1e-6 && pool.live == 0 && pool.evictions == 1, "evicted 0.3 s after the last record");
        // a stage gap of 0.3 s evicts on the next update before any match
        for (unsigned f = 0; f < 5; ++f, now += dt) { const ee::Record r = rec(float(f) * 50.f, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        now += .5; const ee::Record r = rec(300.f, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st);
        expect(st.evicted == 1 && st.created == 1 && only(pool)->count == 1, "a 0.5 s stage gap: evicted, then a fresh ribbon (no bridge)");
    }
    // ------------------------------------------------------------------ the 256 cap, keys, duplicates, skips
    {
        static Pool pool; pool.clear(); UpdateStats st{};
        std::vector<ee::Record> rs; for (unsigned i = 0; i < 300; ++i) rs.push_back(rec(float(i) * 100.f, 0, 40000.f, 1000.f, 1.f, 1000 + i));
        update(pool, rs.data(), unsigned(rs.size()), 1.0, false, 0, nullptr, 1.f, &st);
        expect(pool.live == 256 && st.created == 256 && st.overflow == 44 && pool.overflows == 44, "cap 256, overflow counted");
        update(pool, rs.data(), unsigned(rs.size()), 1.0 + dt, false, 0, nullptr, 1.f, &st);
        expect(st.matched == 256 && st.overflow == 44 && st.created == 0, "the map finds every ribbon again");
        // At the cap a new identity takes the slot of the oldest fading ribbon before it is refused.
        update(pool, rs.data(), 228, 1.0 + 2 * dt, false, 0, nullptr, 1.f, &st); // 228..255 fade from 1.0 + dt
        std::vector<ee::Record> c2(rs.begin(), rs.begin() + 200);              // 200..227 fade from 1.0 + 2 dt
        for (unsigned i = 0; i < 30; ++i) c2.push_back(rec(float(i) * 100.f, 500.f, 40000.f, 1000.f, 1.f, 5000 + i));
        update(pool, c2.data(), unsigned(c2.size()), 1.0 + 3 * dt, false, 0, nullptr, 1.f, &st);
        const auto present = [&](unsigned lo, unsigned hi) { unsigned n = 0; for (unsigned i = lo; i < hi; ++i) n += detail::find(pool, record_key(rs[i])) >= 0; return n; };
        expect(st.created == 30 && st.evicted == 30 && st.overflow == 0 && pool.live == 256 && present(228, 256) == 0 && present(200, 228) == 26,
               "full pool: 30 new nozzles evict the 30 oldest fading ribbons (all 28 of the older group first)");
        for (unsigned i = 0; i < 40; ++i) c2.push_back(rec(float(i) * 100.f, 900.f, 40000.f, 1000.f, 1.f, 6000 + i));
        update(pool, c2.data(), unsigned(c2.size()), 1.0 + 4 * dt, false, 0, nullptr, 1.f, &st);
        expect(st.created == 26 && st.evicted == 26 && st.overflow == 14 && pool.live == 256 && present(200, 228) == 0, "no fading ribbon left: the rest overflow");
        update(pool, c2.data(), unsigned(c2.size()), 1.0 + 5 * dt, false, 0, nullptr, 1.f, &st);
        expect(st.matched == 256 && st.created == 0 && st.overflow == 14 && st.evicted == 0, "after the early evictions the map still finds every live ribbon");
        pool.clear();
        ee::Record a = rec(0, 0, 40000.f, 1000.f, 1.f, 5, 77), b = rec(10, 0, 40000.f, 1000.f, 1.f, 6, 77), c = rec(20, 0, 40000.f, 1000.f, 1.f, 5, 0);
        ee::Record steer = c; steer.flags |= ee::flag_steering; steer.node_handle = 9;
        ee::Record bad = c; bad.flags |= ee::flag_rows_unknown; bad.node_handle = 10;
        const ee::Record set[5] = {a, b, c, steer, bad};
        update(pool, set, 5, 2.0, false, 0, nullptr, 1.f, &st);
        expect(st.created == 2 && st.duplicates == 1 && st.skipped == 2, "serial key (bit 63) and handle+model key; a duplicate identity; RCS and rowless records skipped");
        expect(record_key(a) == (77ull | (1ull << 63)) && record_key(c) == ((20000ull << 32) | 5u), "keys");
    }
    // ------------------------------------------------------------------ the plumes' scene-view filter
    {
        static Pool pool; pool.clear(); UpdateStats st{};
        const ee::Record rs[3] = {rec(0, 0, 40000.f, 1000.f, 1.f, 1), rec(100, 0, 40000.f, 1000.f, 1.f, 2), rec(200, 0, 40000.f, 1000.f, 1.f, 3)};
        const std::uint32_t camera[3] = {5, 9, 5}; const std::uint8_t scene[3] = {1, 1, 0};
        ep::ViewFilter filter; filter.camera = camera; filter.scene = scene; filter.handle = 5;
        update(pool, rs, 3, 1.0, false, 0, nullptr, 1.f, &st, &filter);
        expect(st.created == 1 && st.skipped_other_view == 2 && pool.live == 1 && only(pool)->head[0] == 0.f, "only the scene view's records take a ribbon");
    }
    // ------------------------------------------------------------------ the clock re-base
    {
        static Pool pool; pool.clear(); UpdateStats st{}; double now = 7.0; float x = 0.f;
        for (unsigned f = 0; f < 5500; ++f, now += .2) { const ee::Record r = rec(x, 0, 40000.f, 1000.f, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); x += 2.f; }
        const Ribbon* r = only(pool);
        std::printf("REBASE origin=%.1f last_seen=%.3f v_est=%.4f evictions=%llu\n", pool.origin, double(r ? r->last_seen : -1.f), double(r ? r->speed : 0.f), (unsigned long long)pool.evictions);
        expect(r && pool.origin > 7.0 + 1000.0 && r->last_seen <= time_rebase && near(r->speed, 10.f, .01) && pool.evictions == 0, "the clock re-bases past 1,024 s and keeps v_est");
    }
    // ------------------------------------------------------------------ the strip
    {
        static Pool pool; pool.clear(); double now = 0.0; const float value = 300.f, Z = 20000.f; UpdateStats st{};
        for (unsigned f = 0; f < 90; ++f, now += dt) { const ee::Record r = rec(1200.f * float(f) * dt, 0, Z, value, 1.f); update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        std::vector<Vertex> out(vertices_per_ribbon); BuildStats bs{};
        const unsigned n = build(pool, v, ep::Preset::standard, now - dt, out.data(), 1, &bs);
        const float ppu = 1.7f * 540.f / Z;
        auto width_px = [&](unsigned k) { float d = 0; for (int j = 0; j < 3; ++j) { const float c = out[2 * k + 1].position[j] - out[2 * k].position[j]; d += c * c; } return std::sqrt(d) * ppu; };
        const float head_px = width_px(0), want_head = 2.f * .3f * value * ppu;
        const unsigned last = bs.points - 1;
        std::printf("STRIP points=%u head_px=%.2f want=%.2f tail_px=%.2f radiance_head=%.3f radiance_tail=%.3f floored=%u\n", bs.points, double(head_px), double(want_head), double(width_px(last)), double(out[0].shape[0]), double(out[2 * last].shape[0]), bs.floored);
        expect(n == 1 && bs.vertices == vertices_per_ribbon && near(head_px, want_head, 1e-3), "half-width 0.6 x the nozzle's half-width at the nozzle");
        expect(near(width_px(last), 3.f, 1e-3) && bs.floored >= 1, "the tail held at the 3 px floor");
        // After flight E the plumes' distance law: the nozzle (0.5 x 300 = 150 units) projects 6.9 px at 20,000.
        float fw = 0.f;
        ep::distance_weight(ep::default_look, .5f * value * ppu, &fw);
        expect(fw > .5f && fw < .6f && bs.far_ribbons == 1, "the nozzle under 12 px: the distance law's factor, counted far_ribbons");
        expect(near(out[0].shape[0], .9f * fw, 1e-5) && out[2 * last].shape[0] == 0.f && out[0].strip[0] == 0.f && near(out[2 * last].strip[0], 1.f, 1e-6), "I_ribbon(1) = 0.9 x the distance law at the nozzle, 0 at the tail");
        bool degenerate = true; for (unsigned k = bs.points; k < points_per_ribbon; ++k) degenerate = degenerate && !std::memcmp(&out[2 * k], &out[2 * last], sizeof(Vertex));
        expect(degenerate, "unused points repeat the last (zero-area triangles)");
        expect(out[0].shape[1] == Z && out[0].shape[2] == value && out[0].strip[1] == -1.f && out[1].strip[1] == 1.f, "centre depth, SOFT base, across");
        std::uint16_t ix[96 * 2]; write_indices(ix, 2);
        expect(ix[0] == 0 && ix[1] == 1 && ix[2] == 2 && ix[5] == 3 && ix[96] == 34 && ix[96 + 5] == 37, "the strip's indices");
        // strong preset: x1.5 radiance
        build(pool, v, ep::Preset::strong, now - dt, out.data(), 1, &bs); expect(near(out[0].shape[0], 1.35f * fw, 1e-5), "preset strong: x1.5");
        // The plume's nozzle is the floored value's (the plumes' look and ship radii): a 100 secondary of a radius-5,000
        // ship (scale 0.5 x k 0.1 x 5,000 = 250) carries the nozzle 125; without radii its own 50.
        {
            static Pool fp; fp.clear(); const float rad = 5000.f; UpdateStats fs{};
            const ee::Record q = rec(0, 0, Z, 100.f, 1.f);
            update(fp, &q, 1, 1.0, false, 0, nullptr, 1.f, &fs, nullptr, &ep::default_look, &rad);
            float with = 0.f, without = 0.f;
            for (const auto& r : fp.ribbons) if (r.live) with = r.nozzle;
            fp.clear();
            update(fp, &q, 1, 1.0, false, 0, nullptr, 1.f, &fs);
            for (const auto& r : fp.ribbons) if (r.live) without = r.nozzle;
            expect(near(with, 125.f, 1e-5) && near(without, 50.f, 1e-5), "the ribbon's nozzle: Look::nozzle_width x the floored value (125), else x its own (50)");
        }
        // the chase cap: a ribbon passing close to the camera is held to 0.12 H with radiance down to 0.5
        static Pool close; close.clear(); now = 0.0;
        for (unsigned f = 0; f < 60; ++f, now += dt) { const ee::Record r = rec(30.f, 0, 40.f + 600.f * float(f) * dt, 400.f, 1.f); update(close, &r, 1, now, false, 0, nullptr, 1.f, &st); }
        build(close, v, ep::Preset::standard, now - dt, out.data(), 1, &bs);
        float widest = 0; for (unsigned k = 0; k < bs.points; ++k) { const float z = std::max(out[2 * k].shape[1], 1.f); float d = 0; for (int j = 0; j < 3; ++j) { const float c = out[2 * k + 1].position[j] - out[2 * k].position[j]; d += c * c; } widest = std::max(widest, std::sqrt(d) * 1.7f * 540.f / z); }
        std::printf("CHASE capped=%u widest_px=%.1f cap_px=%.1f\n", bs.capped, double(widest), double(.12f * 1080.f));
        expect(bs.capped >= 1 && widest <= .12f * 1080.f + .5f, "the near-camera cap holds the strip to 0.12 H");
        // fog on the colour
        ep::View fogged = v; const float white[3] = {1, 1, 1}; fr::fog_transmittance_law(2.5e-6f * 8.f * 20.f, .12f, white, .6f, &fogged.fog);
        build(pool, fogged, ep::Preset::standard, double(pool.origin) + double(only(pool)->last_seen), out.data(), 1, &bs);
        float t[3]; float dist = std::sqrt(only(pool)->head[0] * only(pool)->head[0] + Z * Z); fr::fog_transmittance(fogged.fog, dist, t);
        expect(bs.fogged == 1 && std::abs(int((out[0].tint >> 16) & 255u) - int(t[0] * 255.f + .5f)) <= 1, "ribbon colour x T at the nozzle's distance");
    }
    // ------------------------------------------------------------------ cost: update + build at 30 / 100 ribbons
    for (const unsigned count : {30u, 100u}) {
        static Pool pool; pool.clear();
        std::vector<ee::Record> rs; for (unsigned i = 0; i < count; ++i) rs.push_back(rec(0, 0, 0, 200.f + 50.f * float(i), .5f + .5f * float(i % 2), 100 + i));
        std::vector<Vertex> out(vertices_per_ribbon * count); std::vector<double> us; double now = 0.0;
        for (unsigned f = 0; f < 400; ++f, now += dt) {
            for (unsigned i = 0; i < count; ++i) { const float a = .01f * float(f) + float(i); rs[i].origin[0] = 2000.f * float(i % 10) + 900.f * float(f) * dt; rs[i].origin[1] = 300.f * std::sin(a); rs[i].origin[2] = 8000.f + 1500.f * float(i / 10); }
            const auto t0 = std::chrono::steady_clock::now();
            update(pool, rs.data(), count, now, false, 0, nullptr, 1.f, nullptr);
            build(pool, v, ep::Preset::standard, now, out.data(), count, nullptr);
            if (f >= 100) us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(us.begin(), us.end());
        std::printf("COST ribbons=%u median_us=%.2f\n", count, us[us.size() / 2]);
    }
    std::printf("engine_ribbons_core checks=%u failed=%u\n", checks, failed);
    return failed ? 1 : 0;
}
'''


def compiler():
    found = shutil.which('clang++') or shutil.which('c++')
    if found is None:
        raise unittest.SkipTest('A host C++ compiler is required')
    return found


class EngineRibbonsCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory(prefix='x3-engine-ribbons-host-') as directory:
            source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'engine_ribbons_host'
            source.write_text(HARNESS)
            built = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
        cls.returncode, cls.lines = run.returncode, run.stdout.splitlines()

    def test_checks_pass(self):
        self.assertEqual(self.returncode, 0, '\n'.join(l for l in self.lines if l.startswith('FAIL')))
        self.assertRegex(self.lines[-1], r'^engine_ribbons_core checks=\d+ failed=0$')

    def test_compiled_occupancy_matches_the_recipe(self):
        import fog_field_recipe as recipe
        line = next(l for l in self.lines if l.startswith('OCCUPANCY'))
        table = {int(k): float(v) for k, v in (p.split(':') for p in line.split()[1:])}
        want = {p['id']: p['occupancy'] for p in recipe.PROFILES.values()}
        for pid in range(16):
            self.assertAlmostEqual(table[pid], want.get(pid, 0.0), places=6, msg=f'profile {pid}')

    def test_cost(self):
        rows = {int(m.group(1)): float(m.group(2)) for l in self.lines if (m := re.match(r'COST ribbons=(\d+) median_us=([\d.]+)', l))}
        self.assertEqual(sorted(rows), [30, 100])
        self.assertLess(rows[100], 100.0)  # the 0.1 ms budget (host clang -O2; the Wine number is in the fixture record)


class Wiring(unittest.TestCase):
    def test_wiring(self):
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        self.assertIn('if(in.cut&&ribbons_)ribbons_->note_cut();', motion)
        self.assertIn('if(plumes_requested_&&engine_plumes_arm(hdr_scene!=nullptr,depth,in.width,in.height)&&'
                      '(engine_ring_->count||engine_far_jets::count()||engine_ribbons_live())){in.stage_callback=&MotionOutput::engine_plumes_callback;', motion)
        # the ribbons take the plumes' scene-view filter
        self.assertIn('scale,&r.update,f.filter.camera&&f.filter.scene?&f.filter:nullptr,f.look,f.radii,f.travel);',
                      source_text(ROOT / 'src/renderer/engine_ribbons_pass.cpp'))
        self.assertIn('if(ribbons_)taa_call([&]{ribbons_->before_reset();});', motion)
        self.assertIn('if(ribbons_)ribbons_->after_reset(result);', motion)
        self.assertIn('#include "motion_output_engine_ribbons_inc.h"', motion)
        plumes = source_text(ROOT / 'src/proxy/motion_output_engine_plumes_inc.h')
        self.assertIn('engine_plumes_fog(&in.view.fog);', plumes)
        self.assertIn('const HRESULT ribbons=run_engine_ribbons(in);', plumes)
        self.assertIn('attach_engine_ribbons();', plumes)
        self.assertIn('ribbons_->detach();', plumes)
        effects = source_text(ROOT / 'src/proxy/motion_output_engine_effects_inc.h')
        self.assertIn('engine_load_epoch_=life.load_epoch;', effects)
        fog = source_text(ROOT / 'src/proxy/motion_output_fog_inc.h')
        self.assertIn('if(in.density)fog_density_applied_frame_=frame_;', fog)
        self.assertIn('src/renderer/engine_ribbons_pass.cpp', (ROOT / 'CMakeLists.txt').read_text())


class EngineRibbonsFixtureRecord(unittest.TestCase):
    """The tracked Wine record of run_engine_ribbons.py, bound to the production sources it exercised."""
    PATH = ROOT / 'verification/results/bottle-X3/engine-ribbons/summary.json'

    def setUp(self):
        self.record = json.loads(self.PATH.read_text())

    def test_passed(self):
        r = self.record
        self.assertTrue(r['passed'])
        self.assertFalse(r['game_launched'])
        self.assertEqual(r['bottle']['name'], 'X3')
        self.assertEqual(r['report']['failed_checks'], [])
        self.assertEqual(r['build']['warnings'], 0)
        g = r['gates_met']
        self.assertGreaterEqual(g['near_survival_min'], 0.9)
        self.assertTrue(g['length_within'])
        self.assertTrue(g['cpu_100_within'])
        self.assertEqual(len(r['report']['timing']), 4)  # 2 sizes x 30 / 100 ribbons

    def test_bound_to_its_production_sources(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import run_engine_ribbons as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}(-dirty)?$')
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_engine_ribbons.py')


if __name__ == '__main__':
    unittest.main()
