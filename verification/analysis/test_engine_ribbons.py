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
// The plume's length without its pulse (no radii, no travel): max(z, idle_length) x value along the record's axis;
// its visible fraction u_vis (the tail law) and the anchor 0.6 u_vis L_bar.
static float lbar(const ee::Record& r) { return std::max(r.z, ep::default_look.idle_length) * r.size; }
static float uvis() { float u = 0.f; visible_fraction(ep::default_look, &u); return u; }
static float anchor_of(const ee::Record& r) { return anchor_fraction * uvis() * lbar(r); }
static bool at_end(const float p[3], const ee::Record& r, double rel) { const float L = anchor_of(r); bool ok = true; for (int j = 0; j < 3; ++j) ok = ok && std::fabs(p[j] - (r.origin[j] + r.axis[j] * L)) <= rel * L; return ok; }
static float tip_half(float value) { float w = 0.f; ep::law::width(ep::default_look, anchor_fraction * uvis(), &w); return w * ep::default_look.nozzle_width * value; }
// The reviewer's fold counter on the built strip: a run of the centre line toward the nozzle along the record's axis.
// Returns -1 when nothing is drawn, else the number of forward runs.
static int folds(const Ribbon& q, const Pool& p, const ee::Record& r, const ep::View& v, double now) {
    std::vector<Vertex> out(vertices_per_ribbon); BuildStats st{};
    if (!build_ribbon(q, q.seen == p.serial, float(now - p.origin), v, 1.f, out.data(), &st)) return -1;
    int n = 0; float previous = -1e30f;
    for (unsigned k = 0; k < points_per_ribbon; ++k) {
        float along = 0.f; for (int j = 0; j < 3; ++j) along += (.5f * (out[2 * k].position[j] + out[2 * k + 1].position[j]) - r.origin[j]) * r.axis[j];
        if (along < previous - 1e-3f * r.size) ++n;
        previous = along;
    }
    return n;
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
        expect((pv[0].tint & 0xffffffu) == 0xffffffu && pv[0].fog == 0xffffffffu && bs.fogged == 0, "plume builder: no fog without the law");
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
        float kc = 1.f; near_cap_factor(ep::default_look, v, 50000.f, 0.f, r->plume_length, r->plume_value, &kc); // the 30,000's plume is capped at 50,000
        expect(near(drawn, r->length + (r->visible - r->anchor) * kc, 1e-3), "the strip is cut at the fade-in (x the plume's cap) + L");
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
    // ------------------------------------------------------------------ origin at the plume's end (engine-trail-origin.md 6)
    {
        // Straight at constant throttle: the head is the plume's end, the first sample L_bar behind the nozzle on the axis.
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{}; const float value = 1000.f, speed = 4000.f;
        ee::Record r = rec(0, 0, 40000.f, value, .6f); const float L = anchor_of(r); // z 1.3: L_bar 1,300, anchor 661
        update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st);
        const Ribbon* q = only(pool);
        const bool first = q && q->count == 1 && q->at(0).p[0] == -L && q->at(0).p[1] == 0.f && q->at(0).p[2] == 40000.f && q->at(0).o[0] == 0.f;
        bool heads = true, offsets = true; unsigned jumps = 0;
        for (unsigned f = 1; f < 120; ++f) { now += dt; r.origin[0] = speed * float(f) * dt; update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); jumps += st.jumps; heads = heads && at_end(only(pool)->head, r, 1e-5); }
        q = only(pool);
        for (unsigned b = 0; b < q->count; ++b) { float d; detail::distance(q->at(b).p, q->at(b).o, &d); offsets = offsets && near(d, L, 1e-5) && near(q->at(b).o[0] - q->at(b).p[0], L, 1e-5); }
        std::printf("ORIGIN straight anchor=%.1f head_x=%.1f nozzle_x=%.1f v_est=%.1f length=%.1f samples=%u\n", double(L), double(q->head[0]), double(r.origin[0]), double(q->speed), double(q->length), q->count);
        expect(first, "straight: the first sample lies the anchor behind the nozzle on the axis");
        expect(heads && offsets && jumps == 0, "straight: the head is the anchor every frame, every sample the anchor behind its nozzle");
        expect(near(q->speed, speed, 1e-3) && near(q->length, .5 * speed * .6, 1e-3), "straight: v_est and L unchanged by the offset");
        std::vector<Vertex> out(vertices_per_ribbon); BuildStats bs{};
        const bool built = build_ribbon(*q, true, float(now - pool.origin), v, 1.f, out.data(), &bs);
        float hw = 0.f; for (int j = 0; j < 3; ++j) { const float c = out[1].position[j] - out[0].position[j]; hw += c * c; } hw = .5f * std::sqrt(hw);
        std::printf("ORIGIN head_half_width=%.2f tip_half_width=%.2f\n", double(hw), double(tip_half(value)));
        expect(built && near(hw, tip_half(value), 1e-4) && near(q->head_width, tip_half(value), 1e-6) && near(q->at(0).half_width, tip_half(value), 1e-6), "straight: the head's half-width is the plume's at the anchor");
    }
    {
        // Turning: a circle of radius R = L_bar (the plume's end sweeps sqrt(2) x faster than the nozzle).
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{}; const float value = 1000.f, R = 2000.f, w = 2.f;
        ee::Record r = rec(R, 0, 40000.f, value, 1.f); const float L = anchor_of(r); // z 2: L_bar 2,000, anchor 1,017
        bool heads = true, clear = true; unsigned jumps = 0; float worst = 1e30f;
        for (unsigned f = 0; f < 120; ++f, now += dt) {
            const float a = w * float(f) * dt;
            r.origin[0] = R * std::cos(a); r.origin[1] = R * std::sin(a); r.axis[0] = std::sin(a); r.axis[1] = -std::cos(a); r.axis[2] = 0.f;
            update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); jumps += st.jumps;
            const Ribbon* q = only(pool); heads = heads && at_end(q->head, r, 1e-4);
            // No sample on the segment from the nozzle to the anchor (its last 1 % excepted: the newest sample).
            for (unsigned b = 0; b < q->count; ++b) {
                const float* p = q->at(b).p; float t = 0.f; for (int j = 0; j < 3; ++j) t += (p[j] - r.origin[j]) * r.axis[j];
                t = std::min(std::max(t, 0.f), .99f * L); float d2 = 0.f; for (int j = 0; j < 3; ++j) { const float c = p[j] - (r.origin[j] + r.axis[j] * t); d2 += c * c; }
                worst = std::min(worst, std::sqrt(d2)); clear = clear && std::sqrt(d2) > .005f * L;
            }
        }
        const Ribbon* q = only(pool);
        std::printf("ORIGIN turn anchor=%.1f nozzle_speed=%.1f end_speed=%.1f v_est=%.1f length=%.1f min_dist_to_segment=%.2f jumps=%u\n", double(L), double(R * w), double(std::sqrt(R * R + L * L) * w), double(q->speed), double(q->length), double(worst), jumps);
        expect(heads && jumps == 0, "turn: the head follows the anchor, no restart");
        expect(clear, "turn: no sample between the nozzle and the anchor");
        expect(near(q->speed, R * w, .01) && near(q->length, .5 * R * w, .01), "turn: v_est and L from the nozzle's path, not the faster anchor");
    }
    {
        // Throttle change: L_bar 0.6 -> 2 value along the axis (the anchor with it), no restart, v_est still the ship's.
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{}; const float speed = 4000.f;
        unsigned jumps = 0, created = 0; ee::Record r = rec(0, 0, 40000.f, 1000.f, .2f);
        float before = 0.f, step_head[3] = {};
        for (unsigned f = 0; f < 120; ++f, now += dt) {
            if (f == 60) { before = only(pool)->head[0] - r.origin[0]; r.s = 1.f; r.z = 2.f; }
            r.origin[0] = speed * float(f) * dt; update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); jumps += st.jumps; created += st.created;
            if (f == 60) std::memcpy(step_head, only(pool)->head, sizeof step_head);
        }
        const Ribbon* q = only(pool);
        std::printf("ORIGIN throttle before=%.1f after=%.1f v_est=%.1f samples=%u jumps=%u\n", double(before), double(step_head[0] - speed * 60.f * dt), double(q->speed), q->count, jumps);
        const double ua = double(anchor_fraction) * uvis();
        expect(near(before, -600. * ua, 1e-4) && near(step_head[0] - speed * 60.f * dt, -2000. * ua, 1e-4) && step_head[1] == 0.f && step_head[2] == 40000.f, "throttle: the head moves along the axis with the anchor");
        expect(jumps == 0 && created == 1 && q->count == samples_per_ribbon && near(q->speed, speed, 1e-3), "throttle: no restart, v_est the nozzle's");
        // A stationary nozzle whose plume end moves 9.5 value (z 0.5 -> 10): no jump (the test is on the nozzle).
        static Pool still; still.clear(); ee::Record s = rec(0, 0, 40000.f, 1000.f, 0.f); s.z = .5f;
        update(still, &s, 1, 0.0, false, 0, nullptr, 1.f, &st); s.z = 10.f; update(still, &s, 1, dt, false, 0, nullptr, 1.f, &st);
        expect(st.jumps == 0 && at_end(only(still)->head, s, 1e-6) && only(still)->speed == 0.f, "an anchor moved past 8 value x 0.51 with the nozzle still: no jump, v_est 0");
        // No fold: a step 0.2 -> 1 at 4,000 u/s and a 1 s ramp 0.2 -> 1 at 500 u/s.
        for (const bool ramp : {false, true}) {
            static Pool fp; fp.clear(); double t0 = 0.0; UpdateStats fs{}; const float sp = ramp ? 500.f : 4000.f, Z = ramp ? 5000.f : 20000.f;
            ee::Record f = rec(0, 0, Z, 1000.f, .2f); int folded = 0, drawn = 0; unsigned dropped = 0, fj = 0;
            for (unsigned i = 0; i < 180; ++i, t0 += dt) {
                const float s1 = ramp ? std::min(1.f, std::max(.2f, .2f + .8f * float(int(i) - 60) / 60.f)) : (i >= 60 ? 1.f : .2f);
                f.s = s1; f.z = .25f + 1.75f * s1; f.origin[0] = sp * float(i) * dt;
                update(fp, &f, 1, t0, false, 0, nullptr, 1.f, &fs); dropped += fs.dropped; fj += fs.jumps;
                const int n = folds(*only(fp), fp, f, v, t0);
                if (n >= 0) { ++drawn; folded += n > 0; }
            }
            std::printf("ORIGIN fold %s speed=%.0f drawn_frames=%d folded_frames=%d dropped=%u jumps=%u\n", ramp ? "ramp" : "step", double(sp), drawn, folded, dropped, fj);
            expect(folded == 0 && drawn > 60 && fj == 0 && dropped > 0, ramp ? "ramp 0.2 -> 1 over 1 s at 500 u/s: 0 folded frames" : "step 0.2 -> 1 at 4,000 u/s: 0 folded frames");
        }
    }
    {
        // Teleport: the nozzle moves 9 value in one update: the ribbon restarts at the new plume end.
        static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{}; unsigned jumps = 0;
        ee::Record r = rec(0, 0, 40000.f, 1000.f, 1.f);
        for (unsigned f = 0; f < 30; ++f, now += dt) { r.origin[0] = 3000.f * float(f) * dt; update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st); jumps += st.jumps; }
        r.origin[1] += 9000.f; update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st);
        const Ribbon* q = only(pool);
        expect(jumps == 0 && st.jumps == 1 && q->count == 1 && at_end(q->head, r, 1e-6) && at_end(q->at(0).p, r, 1e-6) && q->origin[1] == r.origin[1], "teleport: the nozzle's jump restarts the ribbon at the new anchor");
    }
    // ------------------------------------------------------------------ the anchor inside the plume (engine-trail-origin.md 6)
    {
        float u = uvis(), t_peak = 0.f, t_vis = 0.f; for (unsigned i = 0; i <= 64; ++i) { float t; ep::law::tail(ep::default_look, float(i) / 64.f, &t); t_peak = std::max(t_peak, t); }
        ep::law::tail(ep::default_look, u, &t_vis);
        std::printf("ANCHOR u_vis=%.4f u_anchor=%.4f tail_at_u_vis=%.4f of_peak\n", double(u), double(anchor_fraction * u), double(t_vis / t_peak));
        expect(u > .84f && u < .86f && near(t_vis / t_peak, visible_tail, 1e-3), "u_vis: the tail law at 0.1 of its peak (0.85 at the default look)");
        // Per case, frames 30..179: the plume drawn by build_nozzle (a pulse-free look: its L is k x L_bar) and the strip.
        ep::Look nl = ep::default_look; nl.pulse = 0.f;
        static ep::LookTables tables; ep::look_tables(nl, &tables);
        const char* names[5] = {"straight", "turn", "throttle_up", "throttle_down", "chase_own"};
        for (int kind = 0; kind < 5; ++kind) {
            static Pool pool; pool.clear(); double now = 0.0; UpdateStats st{};
            unsigned frames = 0, measured = 0, contained = 0, ended = 0, folded = 0, inside = 0, head_out = 0, capped = 0;
            float min_exit = 2.f, min_end = 2.f, max_head = 0.f, k_err = 0.f, min_k = 1.f;
            for (unsigned f = 0; f < 180; ++f, now += dt) {
                ee::Record r = rec(0, 0, 40000.f, 1000.f, .6f); ep::View w = v; const float t = float(f) * dt;
                if (kind == 0) r.origin[0] = 4000.f * t;
                else if (kind == 1) { const float R = 2000.f, a = 2.f * t; r.s = 1.f; r.z = 2.f; r.origin[0] = R * std::cos(a); r.origin[1] = R * std::sin(a); r.axis[0] = std::sin(a); r.axis[1] = -std::cos(a); }
                else if (kind == 2 || kind == 3) { const float s1 = (f >= 90) == (kind == 2) ? 1.f : .2f; r.s = s1; r.z = .25f + 1.75f * s1; r.origin[0] = 4000.f * t; }
                else { r = rec(0, -150.f, 800.f + 500.f * t, 400.f, 1.f); r.axis[0] = 0.f; r.axis[2] = -1.f; w.rows[11] = -500.f * t; } // the camera 800 behind, 150 above, flying +z at 500 u/s
                update(pool, &r, 1, now, false, 0, nullptr, 1.f, &st, nullptr, &nl);
                const Ribbon* q = only(pool); if (!q) continue;
                for (unsigned b = 0; b < q->count; ++b) { // a sample on the segment nozzle .. 0.99 anchor
                    const float* p = q->at(b).p; float a = 0.f; for (int j = 0; j < 3; ++j) a += (p[j] - r.origin[j]) * r.axis[j];
                    a = std::min(std::max(a, 0.f), .99f * q->anchor); float d2 = 0.f; for (int j = 0; j < 3; ++j) { const float c = p[j] - (r.origin[j] + r.axis[j] * a); d2 += c * c; }
                    inside += std::sqrt(d2) < .005f * q->anchor;
                }
                if (f < 30) continue;
                ++frames;
                std::vector<Vertex> out(vertices_per_ribbon); BuildStats bs{};
                if (!build_ribbon(*q, true, float(now - pool.origin), w, 1.f, out.data(), &bs, nl)) continue;
                ep::Vertex pv[ep::vertices_per_nozzle]; ep::BuildStats ps{};
                if (!ep::build_nozzle(r, nullptr, w, nl, tables, 1.f, float(now), 0.f, pv, &ps)) continue;
                float o[3], a[3]; ep::detail::to_view(w, r.origin, o); ep::detail::rotate(w, r.axis, a);
                const float kp = pv[0].local[2] / q->plume_length; // the drawn plume's cap factor
                float kr = 1.f; near_cap_factor(nl, w, o[2], a[2], q->plume_length, q->plume_value, &kr);
                k_err = std::max(k_err, std::fabs(kr - kp) / kp); min_k = std::min(min_k, kp); capped += kp < 1.f;
                const float vis = kp * q->visible, Lp = kp * q->plume_length;
                float mid[points_per_ribbon][3]{}, along[points_per_ribbon]{};
                for (unsigned k = 0; k < bs.points; ++k) { along[k] = 0.f; for (int j = 0; j < 3; ++j) { mid[k][j] = .5f * (out[2 * k].position[j] + out[2 * k + 1].position[j]); along[k] += (mid[k][j] - o[j]) * a[j]; } }
                max_head = std::max(max_head, along[0] / vis); head_out += !(along[0] > 0.f && along[0] < vis);
                // A fold: the strip doubling back (consecutive segments over 90 degrees apart) or running back toward the
                // nozzle while inside the plume's reach (a turn's far tail curving back on its circle is not one).
                bool fold = false;
                for (unsigned k = 1; k < bs.points; ++k) { fold = fold || (along[k - 1] < vis && along[k] < along[k - 1] - 1e-3f * r.size); if (k + 1 < bs.points) { float dd = 0.f; for (int j = 0; j < 3; ++j) dd += (mid[k][j] - mid[k - 1][j]) * (mid[k + 1][j] - mid[k][j]); fold = fold || dd < 0.f; } }
                folded += fold;
                // Along the centre line: the fade-in where the strip leaves the drawn plume's visible body (axial past
                // k L_vis or radial past its half-width), and at the point nearest to the plume's visible end.
                float exit_alpha = -1.f, best = 1e30f, end_alpha = 0.f;
                for (unsigned k = 0; k + 1 < bs.points; ++k) for (unsigned i = 0; i < 64; ++i) {
                    const float sfrac = float(i) / 64.f; float pt[3], ax = 0.f, rho2 = 0.f;
                    for (int j = 0; j < 3; ++j) { pt[j] = mid[k][j] + (mid[k + 1][j] - mid[k][j]) * sfrac; ax += (pt[j] - o[j]) * a[j]; }
                    for (int j = 0; j < 3; ++j) { const float c = pt[j] - o[j] - a[j] * ax; rho2 += c * c; }
                    const float al = out[2 * k].strip[2] + (out[2 * k + 2].strip[2] - out[2 * k].strip[2]) * sfrac;
                    float wu = 0.f; ep::law::width(nl, std::min(std::max(ax / Lp, 0.f), 1.f), &wu); const float half = wu * nl.nozzle_width * q->plume_value * kp;
                    if (exit_alpha < 0.f && (ax > vis || ax < 0.f || rho2 > half * half)) exit_alpha = al;
                    float d2 = 0.f; for (int j = 0; j < 3; ++j) { const float c = pt[j] - (o[j] + a[j] * vis); d2 += c * c; }
                    if (d2 < best) { best = d2; end_alpha = al; }
                }
                if (exit_alpha >= 0.f) { ++measured; min_exit = std::min(min_exit, exit_alpha); } else ++contained; // all of it inside the plume
                if (std::sqrt(best) < .05f * vis && along[bs.points - 1] > vis) { ++ended; min_end = std::min(min_end, end_alpha); } // it runs on past the end
            }
            std::printf("ANCHOR %s frames=%u capped=%u min_k=%.3f k_err=%.2g head_over_visible_max=%.3f head_outside=%u exit_alpha_min=%.3f exit_frames=%u contained_frames=%u end_alpha_min=%.3f end_frames=%u folded=%u samples_inside=%u\n",
                        names[kind], frames, capped, double(min_k), double(k_err), double(max_head), head_out, double(min_exit), measured, contained, double(min_end), ended, folded, inside);
            // The turn carries the strip out through the plume's side, never past its visible end (end_frames 0).
            expect(frames == 150 && measured + contained == frames && measured >= 140 && min_exit >= .8f && (kind == 1 ? ended == 0 : ended >= 140 && min_end >= .8f),
                   "anchor: the strip's fade-in >= 0.8 where it leaves the visible plume and at the plume's visible end");
            expect(head_out == 0 && max_head < .61f && folded == 0 && inside == 0 && k_err < 1e-5f, "anchor: the head inside the visible plume (0.6 L_vis, the plume's k), 0 folded frames, no sample between the nozzle and the anchor");
            if (kind == 4) expect(capped == frames && min_k < .3f, "chase own ship: the plume capped every frame (k < 0.3), the head follows it");
        }
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
        expect(st.cut_clear && pool.cut_clears == 1 && r && r->count == 1 && at_end(r->at(0).p, moved, 1e-6) && r->at(0).o[0] == -50000.f && r->length == 0.f, "cut: cleared, the new ribbon starts at the new position");
        now += dt; const ee::Record next = rec(-50000.f + 3000.f * dt, 0, 40000.f, 1000.f, 1.f); update(pool, &next, 1, now, false, 0, nullptr, 1.f, &st);
        float far = 0; for (unsigned b = 0; b < only(pool)->count; ++b) far = std::max(far, std::fabs(only(pool)->at(b).o[0] + 50000.f));
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
        // The radiance at the first point past the fade-in (the geometry stands still while it fades).
        auto lit = [&]() { unsigned k = 0; while (k + 1 < points_per_ribbon && out[2 * k].strip[2] < 1.f) ++k; return out[2 * k].shape[0]; };
        const float full = lit();
        unsigned fading_frames = 0; float half = 0.f; double evicted_at = -1;
        const double lost = now - dt;
        for (unsigned f = 0; f < 40; ++f, now += dt) {
            update(pool, nullptr, 0, now, false, 0, nullptr, 1.f, &st);
            if (st.evicted) { evicted_at = now - lost; break; }
            fading_frames += st.fading;
            build(pool, v, ep::Preset::standard, now, out.data(), 1, &bs);
            if (f == 8) half = lit() / full; // 9 frames = 0.15 s after the last record
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
        expect(st.created == 1 && st.skipped_other_view == 2 && pool.live == 1 && only(pool)->origin[0] == 0.f && at_end(only(pool)->head, rs[0], 1e-6), "only the scene view's records take a ribbon");
    }
    // ------------------------------------------------------------------ the own ship (engine_ribbons_own_ship)
    {
        // Three moving nozzles, the middle one the own ship's: no ribbon and no sample over 100 frames, the other two as
        // without flags; the setting on (the pass hands null) gives all three their ribbon.
        static Pool with_own, without, kept, pair; with_own.clear(); without.clear(); kept.clear(); pair.clear();
        const std::uint8_t own[3] = {0, 1, 0}; UpdateStats st{}, sn{}, sk{}, sp{}; double now = 3.0;
        unsigned own_skipped = 0, appended_with = 0, appended_pair = 0, appended_all = 0; bool never = true;
        for (unsigned f = 0; f < 100; ++f, now += dt) {
            ee::Record rs[3]; for (unsigned i = 0; i < 3; ++i) rs[i] = rec(2000.f * float(i) + 3000.f * float(f) * dt, 0, 40000.f, 1000.f, 1.f, 11 + i);
            update(with_own, rs, 3, now, false, 0, nullptr, 1.f, &st, nullptr, nullptr, nullptr, 0.f, nullptr, own);
            update(without, rs, 3, now, false, 0, nullptr, 1.f, &sn);
            update(kept, rs, 3, now, false, 0, nullptr, 1.f, &sk, nullptr, nullptr, nullptr, 0.f, nullptr, nullptr);
            const ee::Record two[2] = {rs[0], rs[2]}; update(pair, two, 2, now, false, 0, nullptr, 1.f, &sp);
            own_skipped += st.own_skipped; appended_with += st.appended; appended_pair += sp.appended; appended_all += sn.appended;
            never = never && detail::find(with_own, record_key(rs[1])) < 0;
        }
        const ee::Record r0 = rec(0, 0, 40000.f, 1000.f, 1.f, 11), r2 = rec(0, 0, 40000.f, 1000.f, 1.f, 13);
        bool same = true;
        for (const ee::Record* q : {&r0, &r2}) {
            const int a = detail::find(with_own, record_key(*q)), b = detail::find(without, record_key(*q));
            same = same && a >= 0 && b >= 0 && with_own.ribbons[a].count == without.ribbons[b].count && with_own.ribbons[a].length == without.ribbons[b].length &&
                   !std::memcmp(with_own.ribbons[a].samples, without.ribbons[b].samples, sizeof without.ribbons[b].samples);
        }
        std::printf("OWN live=%u own_skipped=%u appended=%u pair=%u unflagged=%u kept_live=%u\n", with_own.live, own_skipped, appended_with, appended_pair, appended_all, kept.live);
        expect(never && with_own.live == 2 && own_skipped == 100 && st.created == 0 && st.evicted == 0, "the own ship's record: no ribbon over 100 frames, counted own_skipped");
        expect(same && appended_with == appended_pair && appended_all > appended_with, "the other two unchanged; the own one appended no sample");
        expect(kept.live == 3 && sk.own_skipped == 0, "the setting on (null flags): all three take a ribbon");
        // The flag turns onto a record with a ribbon (the player changed ship): released in that one update.
        const std::uint8_t flipped[3] = {1, 0, 0};
        ee::Record rs[3]; for (unsigned i = 0; i < 3; ++i) rs[i] = rec(2000.f * float(i) + 3000.f * float(100) * dt, 0, 40000.f, 1000.f, 1.f, 11 + i);
        const std::uint64_t evictions = kept.evictions;
        update(kept, rs, 3, now, false, 0, nullptr, 1.f, &sk, nullptr, nullptr, nullptr, 0.f, nullptr, flipped);
        expect(detail::find(kept, record_key(rs[0])) < 0 && kept.live == 2 && sk.evicted == 1 && sk.own_skipped == 1 && kept.evictions == evictions + 1 && sk.matched == 2,
               "the flag on an existing ribbon: released within one update, the others matched");
        unsigned live_slots = 0; for (const auto& r : kept.ribbons) live_slots += r.live;
        now += dt; update(kept, rs, 3, now, false, 0, nullptr, 1.f, &sk);
        expect(live_slots == 2 && sk.created == 1 && sk.matched == 2 && kept.live == 3, "the released slot is free again: the identity starts afresh without the flag");
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
        const float head_px = width_px(0), want_head = std::max(2.f * tip_half(value) * ppu, 3.f);
        const unsigned last = bs.points - 1;
        std::printf("STRIP points=%u head_px=%.2f want=%.2f tail_px=%.2f radiance_head=%.3f radiance_tail=%.3f floored=%u\n", bs.points, double(head_px), double(want_head), double(width_px(last)), double(out[0].shape[0]), double(out[2 * last].shape[0]), bs.floored);
        expect(n == 1 && bs.vertices == vertices_per_ribbon && near(head_px, want_head, 1e-3), "the head's half-width: the plume's at the anchor");
        expect(near(width_px(last), 3.f, 1e-3) && bs.floored >= 1, "the tail held at the 3 px floor");
        // After flight E the plumes' distance law: the nozzle (0.5 x 300 = 150 units) projects 6.9 px at 20,000.
        float fw = 0.f;
        ep::distance_weight(ep::default_look, .5f * value * ppu, &fw);
        expect(fw > .5f && fw < .6f && bs.far_ribbons == 1, "the nozzle under 12 px: the distance law's factor, counted far_ribbons");
        // The fade-in: 0 at the head, linear in arc length to 1 at the inserted point (L_vis), 1 past it; there
        // I_ribbon(1) x (1 - u).
        unsigned kr = 0; while (kr < last && out[2 * kr].strip[2] < 1.f) ++kr;
        const float ramp_world = (only(pool)->visible - only(pool)->anchor);
        double arc = 0.; bool linear = true;
        for (unsigned k = 1; k <= kr; ++k) { double d = 0; for (int j = 0; j < 3; ++j) { const double c = out[2 * k].position[j] + out[2 * k + 1].position[j] - out[2 * k - 2].position[j] - out[2 * k - 1].position[j]; d += .25 * c * c; } arc += std::sqrt(d); linear = linear && near(out[2 * k].strip[2], std::min(1., arc / ramp_world), 1e-3); }
        std::printf("STRIP fade_in_point=%u arc=%.1f fade_in_world=%.1f\n", kr, arc, double(ramp_world));
        expect(kr > 0 && kr < last && linear && near(arc, ramp_world, 1e-3), "the fade-in: linear from 0 at the head to 1 at L_vis (a point there)");
        expect(out[0].shape[0] == 0.f && out[0].strip[2] == 0.f && near(out[2 * kr].shape[0], .9f * fw * (1.f - out[2 * kr].strip[0]), 1e-5) && out[2 * last].shape[0] == 0.f && out[0].strip[0] == 0.f && near(out[2 * last].strip[0], 1.f, 1e-6), "I_ribbon(1) = 0.9 x the distance law x (1 - u) past the fade-in, 0 at the head and the tail");
        bool degenerate = true; for (unsigned k = bs.points; k < points_per_ribbon; ++k) degenerate = degenerate && !std::memcmp(&out[2 * k], &out[2 * last], sizeof(Vertex));
        expect(degenerate, "unused points repeat the last (zero-area triangles)");
        expect(out[0].shape[1] == Z && out[0].shape[2] == value && out[0].strip[1] == -1.f && out[1].strip[1] == 1.f, "centre depth, SOFT base, across");
        std::uint16_t ix[indices_per_ribbon * 2]; write_indices(ix, 2);
        expect(ix[0] == 0 && ix[1] == 1 && ix[2] == 2 && ix[5] == 3 && ix[indices_per_ribbon] == vertices_per_ribbon && ix[indices_per_ribbon + 5] == vertices_per_ribbon + 3, "the strip's indices");
        // strong preset: x1.5 radiance
        build(pool, v, ep::Preset::strong, now - dt, out.data(), 1, &bs); expect(near(out[2 * kr].shape[0], 1.35f * fw * (1.f - out[2 * kr].strip[0]), 1e-5), "preset strong: x1.5");
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
            float tip = 0.f; ep::width_at_tip(ep::default_look, &tip); fp.clear(); update(fp, &q, 1, 1.0, false, 0, nullptr, 1.f, &fs, nullptr, &ep::default_look, &rad);
            float wa = 0.f; ep::law::width(ep::default_look, anchor_fraction * uvis(), &wa); (void)tip;
            expect(near(only(fp)->head_width, wa * 125.f, 1e-5), "the head's half-width at the anchor, of the floored value");
            expect(near(with, 125.f, 1e-5) && near(without, 50.f, 1e-5), "the ribbon's nozzle: Look::nozzle_width x the floored value (125), else x its own (50)");
        }
        // After Run 129 A: a smaller co-located layer of one parent (100 and 50, 30 apart on one axis) takes its own
        // ribbon at its natural nozzle (25) beside the floored larger one (125); without parents both floor (125; the 50
        // to its cap 4 x 50: 100).
        {
            static Pool lp; const float rad[2] = {5000.f, 5000.f}; const std::uint32_t par[2] = {9, 9}; UpdateStats ls{};
            const ee::Record q[2] = {rec(0, 0, Z, 100.f, 1.f, 7), rec(0, 30.f, Z, 50.f, 1.f, 8)};
            float big = 0.f, small = 0.f, big_np = 0.f, small_np = 0.f; unsigned live = 0, live_np = 0;
            lp.clear();
            update(lp, q, 2, 1.0, false, 0, nullptr, 1.f, &ls, nullptr, &ep::default_look, rad, 0.f, par);
            const unsigned unfloored = ls.unfloored;
            for (const auto& r : lp.ribbons) if (r.live) { ++live; (r.value > 75.f ? big : small) = r.nozzle; }
            lp.clear();
            update(lp, q, 2, 1.0, false, 0, nullptr, 1.f, &ls, nullptr, &ep::default_look, rad);
            for (const auto& r : lp.ribbons) if (r.live) { ++live_np; (r.value > 75.f ? big_np : small_np) = r.nozzle; }
            expect(live == 2 && unfloored == 1 && near(big, 125.f, 1e-5) && near(small, 25.f, 1e-5) && live_np == 2 &&
                       ls.unfloored == 0 && near(big_np, 125.f, 1e-5) && near(small_np, 100.f, 1e-5),
                   "a co-located layer: its own ribbon at its natural nozzle (25), the larger floored (125); no parents: both floored (125, 100)");
        }
        // the chase cap: a ribbon passing close to the camera is held to 0.12 H with radiance down to 0.5 (a record
        // without an axis: its head is the nozzle, the plume's own cap does not shrink it)
        static Pool close; close.clear(); now = 0.0;
        for (unsigned f = 0; f < 60; ++f, now += dt) { ee::Record r = rec(30.f, 0, 40.f + 600.f * float(f) * dt, 400.f, 1.f); r.axis[0] = 0.f; update(close, &r, 1, now, false, 0, nullptr, 1.f, &st); }
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
        self.assertIn('scale,&r.update,f.filter.camera&&f.filter.scene?&f.filter:nullptr,f.look,f.radii,f.travel,f.parents,'
                      'frame.own_ship?nullptr:f.own);',
                      source_text(ROOT / 'src/renderer/engine_ribbons_pass.cpp'))
        self.assertIn('if(ribbons_)taa_call([&]{ribbons_->before_reset();});', motion)
        self.assertIn('if(ribbons_)ribbons_->after_reset(result);', motion)
        # engine_ribbons_own_ship: read once at configure, carried by every frame
        self.assertIn('f.own_ship=ribbons_own_ship_;', source_text(ROOT / 'src/proxy/motion_output_engine_ribbons_inc.h'))
        self.assertIn('x3m::config::get(L"X3M_ENGINE_RIBBONS_OWN_SHIP",text,8);', source_text(ROOT / 'src/proxy/motion_output_engine_plumes_inc.h'))
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
