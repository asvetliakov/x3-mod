"""Engine plumes, phase 2 (X3M_ENGINE_EFFECTS=plumes; docs/architecture/engine-effects-modern.md sections 3-6): the
portable core src/proxy/engine_plumes_core.h compiled on the host (the preset parser, the Ctrl+Alt+F6 latch, the
record -> vertex builder: geometry, throttle law, presets, the screen minimums, the near-camera cap and fade, the RCS
puffs, the cull rules, capacity, tint and the body table's colours, the flicker, the build's cost), the production
Ctrl+Alt+F6 block of capture.cpp executed with stubbed keys, the preset's schema entry and launcher option, the
wiring, and the tracked Wine record of run_engine_plumes.py bound to its production sources.
"""
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402
from source_text import source_text  # noqa: E402
from verification.analysis.test_config_schema import hermetic_launcher, launch_env  # noqa: E402

HARNESS = r'''
#include "engine_plumes_core.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::engine_plumes;
namespace ee = x3m::engine_effects::core;
static unsigned failed = 0, checks = 0;
static void expect(bool ok, const char* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
static bool near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol * (std::fabs(b) > 1.f ? std::fabs(b) : 1.f); }
static View view() { View v; const float r[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; std::memcpy(v.rows, r, sizeof r); v.m00 = 1.7f * 1080.f / 1920.f; v.m11 = 1.7f; v.height = 1080.f; v.near_z = 1.f; return v; }
static float ppu(float z) { return 1.7f * 540.f / z; }
static ee::Record rec(float x, float y, float z, float ax, float ay, float az, float value, float zs, unsigned flags = unsigned(ee::white) << ee::cluster_shift) {
    ee::Record r{}; const float n = std::sqrt(ax * ax + ay * ay + az * az);
    r.origin[0] = x; r.origin[1] = y; r.origin[2] = z; r.axis[0] = ax / n; r.axis[1] = ay / n; r.axis[2] = az / n;
    r.size = value; r.z = zs; float s = (zs - .25f) / 1.75f; r.s = s < 0 ? 0 : s > 1 ? 1 : s; r.body = -1; r.flags = std::uint16_t(flags); r.node_handle = 7; r.model = 20000;
    return r;
}
static float flick(const ee::Record& r, std::uint32_t frame) { float f = 1; flicker(record_seed(r), frame, &f); return f; }
static ee::Body* g_body = nullptr;
static const ee::Body* lookup(int) { return g_body; }
int main() {
    // ----------------------------------------------------------- presets
    Preset p = Preset::strong;
    expect(parse_preset("restrained", &p) && p == Preset::restrained && parse_preset("default", &p) && p == Preset::standard &&
           parse_preset("strong", &p) && p == Preset::strong, "the three words");
    expect(parse_preset(L"default", &p) && p == Preset::standard && parse_preset(L"restrained", 10, &p) && p == Preset::restrained, "wide");
    const char* refused[] = {"", "Default", "STRONG", " strong", "strong ", "strongs", "standard", "1", "default\n", "restrained,strong"};
    for (const char* t : refused) { p = Preset::strong; expect(!parse_preset(t, &p) && p == Preset::strong, t); }
    expect(!std::strcmp(preset_name(Preset::restrained), "restrained") && !std::strcmp(preset_name(Preset::standard), "default") &&
           !std::strcmp(preset_name(Preset::strong), "strong") && default_preset == Preset::standard, "names");
    expect(next_preset(Preset::restrained) == Preset::standard && next_preset(Preset::standard) == Preset::strong &&
           next_preset(Preset::strong) == Preset::restrained, "cycle");
    float sc[3]; for (unsigned i = 0; i < 3; ++i) preset_scale(Preset(i), &sc[i]);
    expect(sc[0] == .6f && sc[1] == 1.f && sc[2] == 1.5f, "scales 0.6 / 1 / 1.5");
    // ----------------------------------------------------------- the F6 latch
    {
        PresetKey k; unsigned n = 0;
        auto f = [&](bool focus, bool c, bool a, bool s, bool f6) { n += k.step(focus, c, a, s, f6); };
        for (int i = 0; i < 5; ++i) f(true, true, true, false, true); // held: one press
        expect(n == 1, "held F6 is one press");
        f(true, true, true, false, false); f(true, true, true, false, true); expect(n == 2, "second press");
        f(true, true, true, false, false); f(true, true, true, true, true); expect(n == 2, "Shift held: no press");
        f(true, false, false, false, false); f(true, false, false, false, true); f(true, true, true, false, true); expect(n == 2, "F6 before the modifiers: no press");
        f(false, false, false, false, false); f(false, true, true, false, true); f(true, true, true, false, true); expect(n == 2, "unfocused, then focused while held: no press");
        f(true, true, false, false, false); f(true, true, false, false, true); expect(n == 2, "Ctrl+F6 without Alt: no press");
    }
    // ----------------------------------------------------------- indices
    {
        std::uint16_t ix[24]; write_indices(ix, 2);
        const std::uint16_t want[24] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7, 8, 9, 10, 10, 9, 11, 12, 13, 14, 14, 13, 15};
        expect(!std::memcmp(ix, want, sizeof ix), "two quads per nozzle, two triangles per quad");
    }
    const View v = view();
    const float Z = 2000.f, V = 100.f / ppu(Z);
    std::vector<Vertex> out(8 * 16);
    BuildStats st{};
    // ----------------------------------------------------------- side view: the axial quad
    {
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        const unsigned n = build(&r, 1, nullptr, v, Preset::standard, 5, out.data(), 16, &st);
        expect(n == 1 && st.nozzles == 1 && st.vertices == 8 && st.discs == 0 && st.capped == 0 && st.faded == 0, "side view: one nozzle, no disc");
        const float L = 2.f * V, sigma = .5f * V, r0 = .15f * V, px = 1.f / ppu(Z);
        // The trapezoid: 2.25 sigma (+1 px) behind the nozzle and past the tip's 1.125 sigma, width linear in u through
        // (0, max(2.25 sigma, r0) + 1 px) and (front, 1.125 sigma + 1 px).
        const float back = 2.25f * sigma + px, front = L + 1.125f * sigma + px, w0 = (2.25f * sigma > r0 ? 2.25f * sigma : r0) + px,
                    wf = 1.125f * sigma + px, wb = w0 + (w0 - wf) * back / front;
        bool layout = true, plane = true;
        const float a[3] = {-1, 0, 0};
        float nrm[3] = {out[1].position[0] - out[0].position[0], out[1].position[1] - out[0].position[1], out[1].position[2] - out[0].position[2]};
        const float nl = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        for (float& c : nrm) c /= nl;
        plane = std::fabs(nrm[0] * a[0] + nrm[1] * a[1] + nrm[2] * a[2]) < 1e-5f && std::fabs(nrm[2]) < 1e-3f; // across is perpendicular to the axis and to the line of sight
        for (unsigned c = 0; c < 4; ++c) {
            const Vertex& x = out[c];
            for (unsigned j = 0; j < 3; ++j) layout = layout && near(x.position[j], r.origin[j] + a[j] * x.local[0] + nrm[j] * x.local[1], 1e-4f);
            layout = layout && near(x.local[2], L) && near(x.local[3], r0) && near(x.shape[0], sigma) && near(x.shape[1], V) && x.shape[2] == 0.f && x.shape[3] == 0.f;
        }
        expect(layout && plane, "axial quad: position = origin + axis u + side w, L = z value, r0 0.15 value, sigma 0.5 value, kind 0");
        expect(near(out[0].local[0], -back) && near(out[1].local[0], -back) && near(out[2].local[0], front) && near(out[3].local[0], front) &&
               near(std::fabs(out[0].local[1]), wb) && near(std::fabs(out[1].local[1]), wb) && near(std::fabs(out[2].local[1]), wf) &&
               near(std::fabs(out[3].local[1]), wf), "axial extents: the 2.25 local sigma trapezoid");
        // The halo window's support (2.25 local sigma from the segment) and the core (+1 px) inside the trapezoid.
        bool covered = true;
        for (float u = -2.25f * sigma; u <= L + 1.125f * sigma; u += L / 64.f) {
            const float t = u < 0.f ? 0.f : u > L ? 1.f : u / L, reach = 2.25f * sigma * (1.f - .5f * t);
            const float du = u < 0.f ? -u : u > L ? u - L : 0.f, halo = du < reach ? std::sqrt(reach * reach - du * du) : 0.f;
            const float core = u >= 0.f && u <= L ? r0 * (1.f - u / L) + px : 0.f, need = halo > core ? halo : core;
            covered = covered && need <= wb + (wf - wb) * (u + back) / (front + back) + 1e-3f;
        }
        expect(covered, "the trapezoid covers the halo window and the core");
        const float f = flick(r, 5);
        expect(near(out[0].intensity[0], 4.f * f) && near(out[0].intensity[1], .8f) && out[0].intensity[2] == 0.f &&
               out[0].intensity[3] == Z && out[4].intensity[3] == Z, "I_core 4 x flicker, I_halo 0.8 at s 1; the axis's view z; the nozzle's view z");
        expect(out[0].tint == 0xffffffffu && out[0].peak == 0xffffffffu, "cluster white: neutral tint");
        bool collapsed = true;
        for (unsigned c = 5; c < 8; ++c) for (unsigned j = 0; j < 3; ++j) collapsed = collapsed && out[c].position[j] == out[4].position[j];
        expect(collapsed, "side view: the disc collapses to one point");
        std::printf("GEOMETRY side L=%.4f r0=%.4f sigma=%.4f flicker=%.4f\n", double(out[0].local[2]), double(out[0].local[3]), double(out[0].shape[0]), double(f));
    }
    // ----------------------------------------------------------- throttle law and presets (value 60 px: strong's
    // 2 sigma = 90 px stays under the fade band of the 0.12 H cap)
    for (const float zs : {.25f, 1.125f, 2.f})
        for (unsigned pr = 0; pr < 3; ++pr) {
            const float V = 60.f / ppu(Z);
            const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, zs);
            build(&r, 1, nullptr, v, Preset(pr), 9, out.data(), 16, &st);
            const float s = r.s, f = flick(r, 9);
            const bool ok = near(out[0].local[2], zs * V) && near(out[0].shape[0], .5f * V * sc[pr]) &&
                            near(out[0].intensity[0], (1.5f + 2.5f * s) * sc[pr] * f) && near(out[0].intensity[1], (.3f + .5f * s) * sc[pr]);
            char what[96]; std::snprintf(what, sizeof what, "law z=%.3f preset=%s", double(zs), preset_name(Preset(pr)));
            expect(ok, what);
        }
    // ----------------------------------------------------------- head-on and tail-on: the disc and the bias
    {
        const ee::Record away = rec(0, 0, Z, 0, 0, 1, V, 2.f), at = rec(0, 0, Z, 0, 0, -1, V, 2.f);
        build(&away, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        const float sigma = .5f * V, half = 2.25f * sigma;
        bool disc = st.discs == 1 && out[4].shape[3] == 1.f && near(out[4].intensity[0], out[0].intensity[0]) && near(out[4].local[3], .25f * V);
        for (unsigned c = 4; c < 8; ++c) disc = disc && near(std::fabs(out[c].position[0]), half) && near(std::fabs(out[c].position[1]), half) && out[c].position[2] == Z;
        bool finite = true;
        for (unsigned c = 0; c < 4; ++c) for (unsigned j = 0; j < 3; ++j) finite = finite && std::isfinite(out[c].position[j]);
        expect(disc && finite && out[0].shape[2] == 0.f && near(out[0].intensity[2], 1.f), "head-on: full disc weight, finite side axis, no bias");
        build(&at, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        expect(near(out[0].shape[2], .5f * V) && near(out[4].shape[2], .5f * V) && near(out[0].intensity[2], -1.f), "tail-on: the occlusion bias 0.5 value");
        const ee::Record tilt = rec(0, 0, Z, -.5f, 0, .8660254f, V, 2.f);
        build(&tilt, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        expect(near(out[4].intensity[0], out[0].intensity[0] * .8660254f, 1e-3f), "disc weight |axis . to_camera|");
        // Under 0.15 no disc; over 0.15..0.3 its radiance fades in.
        const ee::Record grazing = rec(0, 0, Z, -.99498744f, 0, .1f, V, 2.f), low = rec(0, 0, Z, -.9797959f, 0, .2f, V, 2.f);
        build(&grazing, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        expect(st.discs == 0 && out[4].position[0] == out[5].position[0] && out[4].position[1] == out[6].position[1], "facing 0.1: no disc");
        build(&low, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        expect(st.discs == 1 && near(out[4].intensity[0], out[0].intensity[0] * .2f * (.05f / .15f), 1e-3f), "facing 0.2: the disc faded in to a third");
    }
    // ----------------------------------------------------------- the view filter: the scene view's records only
    {
        const ee::Record rs[5] = {rec(0, 0, Z, -1, 0, 0, V, 2.f), rec(0, 0, Z, -1, 0, 0, V, 2.f), rec(0, 0, Z, -1, 0, 0, V, 2.f),
                                  rec(0, 0, Z, -1, 0, 0, V, 2.f), rec(0, 0, Z, -1, 0, 0, V, 2.f)};
        const std::uint32_t cam[5] = {0x77, 0x99, 0x77, 0x77, 0x99};
        const std::uint8_t scene[5] = {1, 1, 0, 1, 1};
        std::uint32_t handle = 0;
        expect(scene_view_camera(cam, scene, 5, &handle) && handle == 0x77, "scene camera: the most frequent scene-phase handle");
        const std::uint8_t none[5] = {};
        expect(!scene_view_camera(cam, none, 5, &handle), "no scene-phase record: no scene camera");
        const std::uint32_t tie[2] = {0x99, 0x77};
        const std::uint8_t both[2] = {1, 1};
        expect(scene_view_camera(tie, both, 2, &handle) && handle == 0x99, "a tie: the first seen");
        ViewFilter vf; vf.camera = cam; vf.scene = scene; vf.handle = 0x77;
        expect(build(rs, 5, nullptr, v, Preset::standard, 0, out.data(), 16, &st, &vf) == 2 && st.skipped_other_view == 3 && st.nozzles == 2,
               "filter: two scene-view records drawn, another camera and the background phase skipped");
        expect(build(rs, 5, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 5 && st.skipped_other_view == 0, "no filter: every record");
    }
    // ----------------------------------------------------------- screen minimums and culls
    {
        const float zf = 20000.f, vp = 4.f / ppu(zf);
        const ee::Record r = rec(0, 0, zf, -1, 0, 0, vp, .25f);
        expect(build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 1 && near(out[0].local[3] * ppu(zf), 1.5f) &&
               near(out[0].local[2] * ppu(zf), 6.f) && near(out[4].local[3] * ppu(zf), 1.5f), "minimums: core radius 1.5 px, length 6 px");
        const ee::Record tiny = rec(0, 0, zf, -1, 0, 0, 1.f / ppu(zf), 2.f);
        expect(build(&tiny, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 0 && st.culled_small == 1, "under 1.5 px: not drawn");
        const ee::Record behind = rec(0, 0, -50.f, 0, 0, -1, 1.f, 2.f);
        expect(build(&behind, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 0 && st.culled_behind == 1, "behind the camera: not drawn");
        ee::Record unknown = rec(0, 0, Z, -1, 0, 0, V, 2.f); unknown.flags |= ee::flag_rows_unknown;
        ee::Record nan = rec(0, 0, Z, -1, 0, 0, V, 2.f); nan.origin[1] = std::nanf("");
        ee::Record zero = rec(0, 0, Z, -1, 0, 0, V, 2.f); zero.size = 0.f;
        const ee::Record bad[3] = {unknown, nan, zero};
        expect(build(bad, 3, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 0 && st.culled_rows == 3, "no geometry, NaN, zero size: not drawn");
        View broken = v; broken.rows[5] = std::nanf("");
        expect(build(&r, 1, nullptr, broken, Preset::standard, 0, out.data(), 16, &st) == 0, "non-finite view: nothing");
    }
    // ----------------------------------------------------------- RCS puffs
    {
        const unsigned steer = (unsigned(ee::white) << ee::cluster_shift) | ee::flag_steering;
        const ee::Record idle = rec(0, 0, Z, -1, 0, 0, 20.f / ppu(Z), .01f, steer), puff = rec(0, 0, Z, -1, 0, 0, 20.f / ppu(Z), .1f, steer);
        expect(build(&idle, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 0 && st.culled_idle == 1, "RCS below z 0.02: not drawn");
        expect(build(&puff, 1, nullptr, v, Preset::standard, 3, out.data(), 16, &st) == 1 && st.steering == 1 && near(out[0].local[2], .1f * 20.f / ppu(Z)) &&
               near(out[0].intensity[0], 1.5f * .1f * flick(puff, 3)) && near(out[0].intensity[1], .3f * .1f), "RCS: L = z value unlengthened, radiance x z");
    }
    // ----------------------------------------------------------- the near-camera cap and fade
    {
        const float f = 1.7f * 540.f, cap = .12f * 1080.f;
        // Tail-on at the nozzle depth zo, L = 2 value: the width 2 sigma at the tip (zo - L) over the cap is q.
        auto at_q = [&](float q, float zo) { // value with 2 (0.5 value) f / (zo - 2 value) = q cap
            return q * cap * zo / (f + 2.f * q * cap);
        };
        for (const float q : {.7f, .9f, 1.f, 3.f, 40.f}) {
            const float zo = 400.f, val = at_q(q, zo);
            const ee::Record r = rec(0, 0, zo, 0, 0, -1, val, 2.f);
            build(&r, 1, nullptr, v, Preset::standard, 4, out.data(), 16, &st);
            const float weight = q <= .8f ? 1.f : q >= 1.f ? .5f : 1.f - .5f * (q - .8f) / .2f;
            const float sigma = out[0].shape[0], L = out[0].local[2];
            const float width = 2.f * sigma * f / (zo - L);
            const bool ok = near(out[0].intensity[0], 4.f * flick(r, 4) * weight, 2e-3f) && (q > 1.f ? near(width, cap, 2e-3f) && st.capped == 1 : st.capped == 0 && near(sigma, .5f * val)) &&
                            st.faded == (q > .8f ? 1u : 0u);
            char what[64]; std::snprintf(what, sizeof what, "near-camera cap q=%.1f", double(q));
            expect(ok, what);
            std::printf("CAP q=%.2f weight=%.3f width_px=%.2f cap_px=%.2f k=%.4f\n", double(q), double(out[0].intensity[0] / (4.f * flick(r, 4))), double(width), double(cap), double(out[0].shape[1] / val));
        }
        // Side view: the length is free (a long plume across the screen keeps its length).
        const float val = 100.f / ppu(Z);
        const ee::Record lng = rec(0, 0, Z, -1, 0, 0, val, 9.f);
        build(&lng, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        expect(st.capped == 0 && near(out[0].local[2], 9.f * val), "a long plume (brake flare 9 value) is not shortened");
    }
    // ----------------------------------------------------------- capacity
    {
        std::vector<ee::Record> many(5, rec(0, 0, Z, -1, 0, 0, V, 2.f));
        expect(build(many.data(), 5, nullptr, v, Preset::standard, 0, out.data(), 3, &st) == 3 && st.culled_capacity == 2 && st.vertices == 24, "capacity: the rest counted");
    }
    // ----------------------------------------------------------- tint
    {
        ee::Body b{}; b.colour = 1; const float m[3] = {.2f, .5f, 1.f}, pk[3] = {1.f, 1.f, .5f};
        std::memcpy(b.mean, m, sizeof m); std::memcpy(b.peak, pk, sizeof pk); g_body = &b;
        ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f, unsigned(ee::red) << ee::cluster_shift); r.body = 3;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect(out[0].tint == pack_colour(m) && out[0].peak == pack_colour(pk) && out[0].tint == 0xff3380ffu, "the body's mean and peak");
        r.body = -1;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect(out[0].tint == pack_colour(cluster_tint(ee::red)) && out[0].peak == out[0].tint && out[0].tint == 0xffff1212u, "no body: the cluster's tint");
        b.colour = 0; r.body = 3;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect(out[0].tint == pack_colour(cluster_tint(ee::red)), "a body without colours: the cluster's tint");
    }
    // ----------------------------------------------------------- the body table's colours
    {
        static ee::BodyTable t;
        const char* doc = "{\"schema\": 1, \"bodies\": {\"a\": {\"id\": 5, \"value\": 10, \"cluster\": \"cyan\", \"mean_linear\": [0.1, 0.4, 0.8], \"peak_linear\": [0.5, 1.0, 2.0]},"
                          " \"b\": {\"id\": 6, \"value\": 10, \"mean_linear\": null, \"peak_linear\": [1, 1, 1]}, \"c\": {\"id\": 7, \"value\": 10, \"mean_linear\": [0, 0, 0], \"peak_linear\": [0, 0, 0]}}}";
        expect(ee::parse_body_table(doc, std::strlen(doc), &t) && t.count == 3, "colour table parses");
        expect(t.bodies[0].colour == 1 && near(t.bodies[0].mean[0], .125f) && near(t.bodies[0].mean[2], 1.f) && near(t.bodies[0].peak[1], .5f) && near(t.bodies[0].peak[2], 1.f), "normalised to the largest channel");
        expect(t.bodies[1].colour == 0 && t.bodies[2].colour == 1 && t.bodies[2].mean[0] == 1.f, "one colour missing: none; black: white");
        const char* bad = "{\"schema\": 1, \"bodies\": {\"a\": {\"mean_linear\": [0.1, 0.4]}}}";
        expect(!ee::parse_body_table(bad, std::strlen(bad), &t) && t.count == 0, "a two-channel colour is malformed");
    }
    // ----------------------------------------------------------- flicker
    {
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        float lo = 2, hi = 0, step = 0, prev = flick(r, 0);
        for (std::uint32_t fr = 1; fr < 4000; ++fr) { const float x = flick(r, fr); lo = x < lo ? x : lo; hi = x > hi ? x : hi; step = std::fabs(x - prev) > step ? std::fabs(x - prev) : step; prev = x; }
        expect(lo >= .9f && hi <= 1.1f && hi - lo > .1f && step <= .2f * 1.5f / 8.f + 1e-4f && flick(r, 77) == flick(r, 77), "flicker: within +-10 %, slow (8-frame cells), deterministic");
        std::printf("FLICKER min=%.4f max=%.4f max_step=%.4f\n", double(lo), double(hi), double(step));
    }
    // ----------------------------------------------------------- cost of the build
    for (const unsigned count : {30u, 100u, 1024u}) {
        std::vector<ee::Record> rs;
        std::uint32_t seed = 99;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.f; };
        for (unsigned i = 0; i < count; ++i) { const float z = 1500.f + 28500.f * rnd(); rs.push_back(rec((rnd() - .5f) * z, (rnd() - .5f) * z * .5f, z, rnd() - .5f, rnd() - .5f, rnd() - .5f, (3.f + 57.f * rnd()) / ppu(z), .25f + 1.75f * rnd())); }
        std::vector<Vertex> vb(std::size_t(count) * 8);
        std::vector<double> us;
        unsigned drawn = 0;
        for (unsigned rep = 0; rep < 31; ++rep) {
            const auto a = std::chrono::steady_clock::now();
            for (unsigned k = 0; k < 20; ++k) drawn = build(rs.data(), count, nullptr, v, Preset::standard, rep * 20 + k, vb.data(), count, nullptr);
            us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count() / 20.);
        }
        std::sort(us.begin(), us.end());
        std::printf("BUILD records=%u drawn=%u median_us=%.2f\n", count, drawn, us[us.size() / 2]);
    }
    std::printf("engine_plumes_core checks=%u failed=%u\n", checks, failed);
    return failed ? 1 : 0;
}
'''


def compiler():
    found = shutil.which('clang++') or shutil.which('c++')
    if found is None:
        raise unittest.SkipTest('A host C++ compiler is required')
    return found


class EnginePlumesCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory(prefix='x3-engine-plumes-host-') as directory:
            source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'engine_plumes_host'
            source.write_text(HARNESS.replace('#include <vector>', '#include <vector>\n#include <algorithm>'))
            built = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
        cls.returncode, cls.lines = run.returncode, run.stdout.splitlines()

    def test_checks_pass(self):
        self.assertEqual(self.returncode, 0, '\n'.join(l for l in self.lines if l.startswith('FAIL')))
        self.assertRegex(self.lines[-1], r'^engine_plumes_core checks=\d+ failed=0$')

    def test_build_cost(self):
        rows = {int(m.group(1)): float(m.group(2)) for l in self.lines if (m := re.match(r'BUILD records=(\d+) drawn=\d+ median_us=([\d.]+)', l))}
        self.assertEqual(sorted(rows), [30, 100, 1024])
        self.assertLess(rows[100], 100.0)  # the 0.1 ms target at 100 records (host clang -O2; the Wine number is in the record)


F6_BEGIN = '    // Ctrl+Alt+F6 (comparison-hotkeys.md'
F6_END = 'ctx.motion_output.engine_plumes_cycle_preset();\n    }\n'


class PresetHotkey(unittest.TestCase):
    """The production Ctrl+Alt+F6 block of capture.cpp's Present, executed with stubbed keys and focus."""

    def test_block(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        start = capture.index(F6_BEGIN)
        block = capture[start:capture.end(F6_END, start)]
        harness = textwrap.dedent('''
            #include "engine_plumes_core.h"
            #include <cstdio>
            #define VK_F6 0x75
            #define VK_CONTROL 0x11
            #define VK_MENU 0x12
            #define VK_SHIFT 0x10
            static unsigned polls = 0, checks = 0, failures = 0;
            static bool keys[256], focused = true;
            short GetAsyncKeyState(int key) { ++polls; return keys[key & 255] ? short(-32768) : short(0); }
            bool comparison_foreground() noexcept { return focused; }
            namespace x3m {
            struct Motion { bool requested = false; unsigned cycles = 0; bool engine_plumes_requested() const { return requested; }
                            int engine_plumes_cycle_preset() { ++cycles; return 0; } };
            struct Ctx { Motion motion_output; engine_plumes::PresetKey plumes_key; };
            static void frame(Ctx& ctx) {
            @BLOCK@
            }
            }
            #define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line=%d %s\\n", __LINE__, #x); } } while (0)
            static void set(bool c, bool a, bool s, bool f6) { keys[VK_CONTROL] = c; keys[VK_MENU] = a; keys[VK_SHIFT] = s; keys[VK_F6] = f6; }
            int main() {
                x3m::Ctx ctx;
                set(true, true, false, true);
                for (int i = 0; i < 5; ++i) x3m::frame(ctx);
                CHECK(polls == 0 && ctx.motion_output.cycles == 0); // plumes not requested: never polled
                ctx.motion_output.requested = true;
                set(false, false, false, false); polls = 0;
                for (int i = 0; i < 10; ++i) x3m::frame(ctx);
                CHECK(polls == 10); // idle: F6 only, once per frame
                set(true, true, false, true); for (int i = 0; i < 6; ++i) x3m::frame(ctx);
                CHECK(ctx.motion_output.cycles == 1); // a held chord is one press
                set(true, true, false, false); x3m::frame(ctx); set(true, true, false, true); x3m::frame(ctx);
                CHECK(ctx.motion_output.cycles == 2);
                set(true, true, true, false); x3m::frame(ctx); set(true, true, true, true); x3m::frame(ctx);
                CHECK(ctx.motion_output.cycles == 2); // Shift held
                set(false, false, false, false); x3m::frame(ctx); set(false, false, false, true); x3m::frame(ctx); set(true, true, false, true); x3m::frame(ctx);
                CHECK(ctx.motion_output.cycles == 2); // F6 first, modifiers later
                set(false, false, false, false); x3m::frame(ctx); focused = false; set(true, true, false, true); x3m::frame(ctx);
                focused = true; x3m::frame(ctx);
                CHECK(ctx.motion_output.cycles == 2); // unfocused press, focus returns while held
                std::printf("f6 checks=%u failures=%u\\n", checks, failures);
                return failures ? 1 : 0;
            }
        ''').replace('@BLOCK@', str(block))
        with tempfile.TemporaryDirectory(prefix='x3-f6-') as temporary:
            source, executable = Path(temporary) / 'f6.cpp', Path(temporary) / 'f6'
            source.write_text(harness)
            built = subprocess.run([compiler(), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'f6 checks=7 failures=0\n')


class PresetOption(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['engine_effects_preset']
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['choices'], e['launcher'], e['developer']),
                         ('X3M_ENGINE_EFFECTS_PRESET', 'enum', 'engine', None, 'default', ('restrained', 'default', 'strong'),
                          '--engine-effects-preset', False))
        self.assertIn('{"X3M_ENGINE_EFFECTS_PRESET", "engine_effects_preset", Type::Enum, nullptr,',
                      (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn(';engine_effects_preset = default', (ROOT / 'assets/x3m.ini').read_text())

    def test_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_EFFECTS_PRESET', launch_env(module, game, wine))
            self.assertNotIn('X3M_ENGINE_EFFECTS_PRESET', launch_env(module, game, wine, '--engine-effects', 'plumes'))
            for value in ('restrained', 'default', 'strong'):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-effects-preset', value)
                self.assertEqual((env['X3M_ENGINE_EFFECTS'], env['X3M_ENGINE_EFFECTS_PRESET']), ('plumes', value))
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-effects-preset', 'strong')
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--engine-effects-preset', 'Strong')

    def test_read_once_at_load(self):
        module = (ROOT / 'src/proxy/engine_effects.cpp').read_text()
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_EFFECTS_PRESET"', module)), 1)
        self.assertIn('engine_plumes::parse_preset(word, n, &parsed)', module)


class Wiring(unittest.TestCase):
    def test_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('configure_engine_plumes(engine_effects::mode()==engine_effects::core::Mode::plumes&&engine_effects::suppress(),'
                      'engine_effects::preset());', capture)
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        self.assertIn('if(plumes_requested_&&engine_plumes_arm(hdr_scene!=nullptr,depth,in.width,in.height)&&'
                      '(engine_ring_->count||engine_ribbons_live())){in.stage_callback=&MotionOutput::engine_plumes_callback;', motion)
        self.assertIn('plumes_failures_=0;plumes_failed_out_=false;plumes_lane_=nullptr;', motion)  # before_reset
        self.assertIn('if(plumes_)taa_call([&]{plumes_->before_reset();});', motion)
        self.assertIn('if(plumes_)plumes_->after_reset(result);', motion)
        self.assertIn('release_engine_plumes();', motion)
        self.assertIn('#include "motion_output_engine_plumes_inc.h"', motion)
        inc = source_text(ROOT / 'src/proxy/motion_output_engine_plumes_inc.h')
        self.assertIn('plumes_disarmed_until_=frame_+plumes_disarm_frames;', inc)
        self.assertIn('static constexpr std::uint64_t plumes_disarm_frames=64;', source_text(ROOT / 'src/proxy/motion_output.h'))
        self.assertIn('glow=suppressed drawn=%s', inc)
        # Review fixes (2026-10-01): the lane checked at arming, three consecutive failures refuse until Reset, the near
        # plane from the latch, the scene view's records only.
        self.assertIn('lane_desc.Width!=width||lane_desc.Height!=height||lane_desc.Format!=lane_depth_format()', inc)
        self.assertIn('const bool final=++plumes_failures_>=plumes_failure_limit;', inc)
        self.assertIn('note_engine_plumes_state(false,"failed_until_reset");', inc)
        self.assertIn('in.view.near_z=-in.m32/in.m22;', inc)
        self.assertIn('in.filter.handle=scene_camera;', inc)
        self.assertIn('static constexpr unsigned plumes_failure_limit=3;', source_text(ROOT / 'src/proxy/motion_output.h'))
        effects = source_text(ROOT / 'src/proxy/motion_output_engine_effects_inc.h')
        self.assertIn('engine_ring_->scene[slot]=selector_.state()==renderer::BoundaryState::Scene?1u:0u;', effects)
        passes = source_text(ROOT / 'src/renderer/engine_plumes_pass.cpp')
        self.assertIn('if(reset_pending_)return refuse(EnginePlumesStep::Validate,E_FAIL);', passes)
        self.assertIn('caps.MaxVertexIndex<engine_plumes::max_vertices-1u', passes)
        temporal = source_text(ROOT / 'src/renderer/temporal_pass.cpp')
        self.assertLess(temporal.index('hr=call<SceneFn>(BeginScene)(d);'), temporal.index('in.stage_callback(in.stage_context,d)'))
        self.assertIn('src/renderer/engine_plumes_pass.cpp', (ROOT / 'CMakeLists.txt').read_text())


class EnginePlumesFixtureRecord(unittest.TestCase):
    """The tracked Wine record of run_engine_plumes.py, bound to the production sources it exercised."""
    PATH = ROOT / 'verification/results/bottle-X3/engine-plumes/summary.json'

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
        self.assertGreaterEqual(g['core_survival_min'], 0.9)
        self.assertLessEqual(g['trail_dark_px_max'], 3)
        self.assertEqual(len(r['report']['resolve']), 12)  # 2 sizes x 2 skies x 3 speeds
        self.assertEqual(len(r['report']['timing']), 4)    # 2 sizes x 30 / 100 nozzles

    def test_bound_to_its_production_sources(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import run_engine_plumes as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}(-dirty)?$')
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_engine_plumes.py')


if __name__ == '__main__':
    unittest.main()
