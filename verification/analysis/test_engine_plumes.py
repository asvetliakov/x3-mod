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
    // ----------------------------------------------------------- side view: the axial quad (no length pulse)
    Look flat = default_look;
    flat.pulse = 0.f;
    {
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        const unsigned n = build(&r, 1, nullptr, v, Preset::standard, 5.f, out.data(), 16, &st, nullptr, &flat);
        expect(n == 1 && st.nozzles == 1 && st.vertices == 8 && st.discs == 0 && st.capped == 0 && st.faded == 0, "side view: one nozzle, no disc");
        const float L = 2.f * V, nw = .25f * V, px = 1.f / ppu(Z);
        bool layout = true, plane = true;
        const float a[3] = {-1, 0, 0};
        float nrm[3] = {out[1].position[0] - out[0].position[0], out[1].position[1] - out[0].position[1], out[1].position[2] - out[0].position[2]};
        const float nl = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        for (float& c : nrm) c /= nl;
        plane = std::fabs(nrm[0] * a[0] + nrm[1] * a[1] + nrm[2] * a[2]) < 1e-5f && std::fabs(nrm[2]) < 1e-3f; // across is perpendicular to the axis and to the line of sight
        for (unsigned c = 0; c < 4; ++c) {
            const Vertex& x = out[c];
            for (unsigned j = 0; j < 3; ++j) layout = layout && near(x.position[j], r.origin[j] + a[j] * x.local[0] + nrm[j] * x.local[1], 1e-4f);
            layout = layout && near(x.local[2], L) && near(x.local[3], nw) && near(x.shape[0], .55f) && near(x.shape[1], V) && x.shape[2] == 0.f && x.shape[3] == 0.f;
        }
        expect(layout && plane, "axial quad: position = origin + axis x + side y, L = z value, n = value / 4, halo sigma0 0.55 nozzle widths, kind 0");
        // The body's eroded edge (1 + 0.48 erode of w(u)), the halo window (2.25 sigma, sigma = 0.55 w(u) / w(0); half
        // discs behind the nozzle and past the tip) and the ring (u near 0) inside the trapezoid, linear in x.
        auto ss = [](float e0, float e1, float x) { float t = (x - e0) / (e1 - e0); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); };
        auto w_of = [&](float u) {
            const float b = .6f * (1 - .55f * std::exp(-9 * u)) * (1 + .35f * ss(0, .25f, u) * std::exp(-4 * u));
            const float line = .6f + (.04f - .6f) * .45f * u;
            return (line < b + .6f * .45f ? line : b + .6f * .45f) * std::max(1 - 1.6f * .45f * ss(.6f, 1, u), .05f);
        };
        const float w0 = w_of(0), x0 = out[0].local[0], x1 = out[2].local[0], h0 = std::fabs(out[0].local[1]), h1 = std::fabs(out[2].local[1]);
        bool covered = true;
        float worst = 1e9f;
        for (float x = x0; x <= x1; x += (x1 - x0) / 512.f) {
            const float u = x / L, uc = u < 0 ? 0 : u > 1 ? 1 : u, w = w_of(uc), reach = 2.25f * .55f * w / w0 * nw;
            float need = 0;
            if (u >= 0 && u <= 1) need = std::max(1.f + .48f * .57f, 2.25f * .55f / w0) * w * nw;
            else {
                const float d = u < 0 ? -x : x - L;
                need = d < reach ? std::sqrt(reach * reach - d * d) : 0.f;
            }
            if (x >= -.05f * nw && x <= .3f * nw) need = std::max(need, (.46f * 1.2f + 3.f * .0645497f) * nw);
            const float have = h0 + (h1 - h0) * (x - x0) / (x1 - x0);
            covered = covered && need <= have + 1e-3f;
            worst = std::min(worst, have - need);
        }
        expect(covered && x0 <= -2.25f * .55f * nw && x1 >= L, "the trapezoid covers the body, the halo window and the ring");
        // Overdraw against the first look's trapezoid (sigma 0.5 value, 2.25 sigma reach, r0 0.15 value).
        for (const float zs : {.25f, 1.125f, 2.f}) {
            const ee::Record q = rec(0, 0, Z, -1, 0, 0, V, zs);
            build(&q, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st, nullptr, &flat);
            const float area = (std::fabs(out[0].local[1]) + std::fabs(out[2].local[1])) * (out[2].local[0] - out[0].local[0]);
            const float sg = .5f * V, Lo = zs * V, back = 2.25f * sg + px, front = Lo + 1.125f * sg + px, wfirst = std::max(2.25f * sg, .15f * V) + px,
                        wf = 1.125f * sg + px, wb = wfirst + (wfirst - wf) * back / front, old_area = (wb + wf) * (back + front);
            std::printf("AREA z=%.3f old_px2=%.0f new_px2=%.0f ratio=%.3f\n", double(zs), double(old_area * ppu(Z) * ppu(Z)), double(area * ppu(Z) * ppu(Z)), double(area / old_area));
        }
        build(&r, 1, nullptr, v, Preset::standard, 5.f, out.data(), 16, &st, nullptr, &flat);
        expect(near(out[0].intensity[0], 4.f) && near(out[0].intensity[1], .35f) && out[0].intensity[2] == 0.f &&
               out[0].intensity[3] == Z && out[4].intensity[3] == Z, "I_core 4, I_halo 0.35 (hb x lerp(0.3, 1, 1)) at s 1; the axis's view z; the nozzle's view z");
        expect(out[0].tint == 0xffffffffu, "cluster white: neutral tint");
        expect((out[0].params >> 24) == 255u && ((out[0].params >> 16) & 255u) == 255u && ((out[0].params >> 8) & 255u) == seed_byte(r) &&
               (out[0].params & 255u) == unsigned(int(.6f / 4.f * .5f * 255.f + .5f)), "params: s 1, the seed, I_ring / I_core / 2");
        bool collapsed = true;
        for (unsigned c = 5; c < 8; ++c) for (unsigned j = 0; j < 3; ++j) collapsed = collapsed && out[c].position[j] == out[4].position[j];
        expect(collapsed, "side view: the disc collapses to one point");
        std::printf("GEOMETRY side L=%.4f n=%.4f sigma0=%.4f back=%.4f front=%.4f half_back=%.4f half_front=%.4f margin_min=%.4f\n", double(out[0].local[2]),
                    double(out[0].local[3]), double(out[0].shape[0]), double(-x0), double(x1), double(h0), double(h1), double(worst));
    }
    // ----------------------------------------------------------- the length pulse: 1 +- 0.25 about the game's L, mean 1
    {
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        float lo = 9, hi = 0, sum = 0, step = 0, prev = -1;
        const unsigned count = 4000;
        for (unsigned i = 0; i < count; ++i) {
            build(&r, 1, nullptr, v, Preset::standard, float(i) / 60.f, out.data(), 16, &st);
            const float k = out[0].local[2] / (2.f * V);
            lo = std::min(lo, k); hi = std::max(hi, k); sum += k;
            if (prev >= 0) step = std::max(step, std::fabs(k - prev));
            prev = k;
        }
        float again = 0;
        length_pulse(default_look, seed_byte(r), 7.25f, &again);
        float twice = 0;
        length_pulse(default_look, seed_byte(r), 7.25f, &twice);
        expect(lo >= .75f - 1e-4f && hi <= 1.25f + 1e-4f && hi - lo > .2f && std::fabs(sum / count - 1.f) < .05f && step < .05f && again == twice,
               "pulse: within 1 +- 0.25, mean 1 within 5 %, no step over 5 % per 1/60 s, deterministic");
        std::printf("PULSE min=%.4f max=%.4f mean=%.4f max_step=%.4f\n", double(lo), double(hi), double(sum / count), double(step));
        float f = 0, fl = 9, fh = 0, fs = 0;
        for (unsigned i = 0; i < 20000; ++i) {
            const float p[3] = {float(i) * .37f, float(i % 97) * 1.3f, float(i % 13) * 2.1f};
            noise::fbm(p, &f); fl = std::min(fl, f); fh = std::max(fh, f); fs += f;
        }
        expect(fl >= 0.f && fh <= .875f + 1e-5f && std::fabs(fs / 20000.f - .4375f) < .03f, "fbm in [0, 0.875], mean 0.4375 within 0.03");
        std::printf("FBM min=%.4f max=%.4f mean=%.4f\n", double(fl), double(fh), double(fs / 20000.f));
    }
    // ----------------------------------------------------------- throttle law and presets (value 60 px: strong's
    // 2 cap_sigma = 90 px stays under the fade band of the 0.12 H cap)
    for (const float zs : {.25f, 1.125f, 2.f})
        for (unsigned pr = 0; pr < 3; ++pr) {
            const float V = 60.f / ppu(Z);
            const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, zs);
            build(&r, 1, nullptr, v, Preset(pr), 9.f, out.data(), 16, &st);
            const float s = r.s;
            float pulse = 0;
            length_pulse(default_look, seed_byte(r), 9.f, &pulse);
            const bool ok = near(out[0].local[2], zs * V * pulse) && near(out[0].shape[0], .55f * sc[pr]) &&
                            near(out[0].intensity[0], (1.2f + 2.8f * s) * sc[pr]) && near(out[0].intensity[1], .35f * (.3f + .7f * s) * sc[pr]);
            char what[96]; std::snprintf(what, sizeof what, "law z=%.3f preset=%s", double(zs), preset_name(Preset(pr)));
            expect(ok, what);
        }
    // ----------------------------------------------------------- head-on and tail-on: the disc and the bias
    {
        const ee::Record away = rec(0, 0, Z, 0, 0, 1, V, 2.f), at = rec(0, 0, Z, 0, 0, -1, V, 2.f);
        build(&away, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st);
        const float nw = .25f * V, half = std::max(1.f + .48f * .57f, 2.25f * .55f / .54f) * nw * .6f + 1.f / ppu(Z);
        bool disc = st.discs == 1 && out[4].shape[3] == 1.f && near(out[4].intensity[0], out[0].intensity[0]) && near(out[4].local[3], nw);
        for (unsigned c = 4; c < 8; ++c) disc = disc && near(std::fabs(out[c].position[0]), half, 1e-3f) && near(std::fabs(out[c].position[1]), half, 1e-3f) && out[c].position[2] == Z;
        bool finite = true;
        for (unsigned c = 0; c < 4; ++c) for (unsigned j = 0; j < 3; ++j) finite = finite && std::isfinite(out[c].position[j]);
        expect(disc && finite && out[0].shape[2] == 0.f && near(out[0].intensity[2], 1.f), "head-on: full disc weight, the nozzle's width, finite side axis, no bias");
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
        ViewRule rule = ViewRule::none;
        const std::uint8_t none[5] = {};
        expect(scene_view_camera(cam, scene, nullptr, 5, &handle, &rule) && handle == 0x77 && rule == ViewRule::majority,
               "scene camera: the most frequent scene-phase handle (majority rule)");
        expect(scene_view_camera(cam, scene, none, 5, &handle, &rule) && handle == 0x77 && rule == ViewRule::majority,
               "no own-ship record: the majority rule");
        expect(!scene_view_camera(cam, none, none, 5, &handle, &rule) && rule == ViewRule::none, "no scene-phase record: no scene camera");
        const std::uint32_t tie[2] = {0x99, 0x77};
        const std::uint8_t both[2] = {1, 1};
        expect(scene_view_camera(tie, both, nullptr, 2, &handle) && handle == 0x99, "a tie: the first seen");
        // Own rule: a target monitor (0x99) recorded in the scene phase with more jets than the main view (0x55) whose
        // one own-ship jet decides; an own-ship jet outside the scene phase does not.
        const std::uint32_t mon[5] = {0x99, 0x99, 0x55, 0x99, 0x77};
        const std::uint8_t mon_scene[5] = {1, 1, 1, 1, 0};
        const std::uint8_t mon_own[5] = {0, 0, 1, 0, 0};
        expect(scene_view_camera(mon, mon_scene, mon_own, 5, &handle, &rule) && handle == 0x55 && rule == ViewRule::own,
               "own rule: the own ship's camera beats a larger target-monitor view");
        const std::uint8_t own_background[5] = {0, 0, 0, 0, 1};
        expect(scene_view_camera(mon, mon_scene, own_background, 5, &handle, &rule) && handle == 0x99 && rule == ViewRule::majority,
               "an own-ship record outside the scene phase: the majority rule");
        expect(!std::strcmp(view_rule_name(ViewRule::own), "own") && !std::strcmp(view_rule_name(ViewRule::majority), "majority"),
               "rule names");
        ViewFilter vf; vf.camera = cam; vf.scene = scene; vf.handle = 0x77;
        expect(build(rs, 5, nullptr, v, Preset::standard, 0, out.data(), 16, &st, &vf) == 2 && st.skipped_other_view == 3 && st.nozzles == 2,
               "filter: two scene-view records drawn, another camera and the background phase skipped");
        expect(build(rs, 5, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 5 && st.skipped_other_view == 0, "no filter: every record");
    }
    // ----------------------------------------------------------- screen minimums and culls
    {
        const float zf = 20000.f, vp = 4.f / ppu(zf);
        const ee::Record r = rec(0, 0, zf, -1, 0, 0, vp, .25f);
        expect(build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 1 && near(out[0].local[3] * ppu(zf), 3.f) &&
               near(out[0].local[2] * ppu(zf), 6.f) && near(out[4].local[3] * ppu(zf), 3.f), "minimums: nozzle width 3 px, length 6 px");
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
        float pulse = 0;
        length_pulse(default_look, seed_byte(puff), 3.f, &pulse);
        expect(build(&puff, 1, nullptr, v, Preset::standard, 3.f, out.data(), 16, &st) == 1 && st.steering == 1 && near(out[0].local[2], .1f * 20.f / ppu(Z) * pulse) &&
               near(out[0].intensity[0], 1.2f * .1f) && near(out[0].intensity[1], .35f * .3f * .1f), "RCS: L = z value x the pulse, unlengthened, radiance x z");
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
            build(&r, 1, nullptr, v, Preset::standard, 4.f, out.data(), 16, &st, nullptr, &flat);
            const float weight = q <= .8f ? 1.f : q >= 1.f ? .5f : 1.f - .5f * (q - .8f) / .2f;
            const float sigma = cap_sigma * out[0].shape[1], L = out[0].local[2]; // the cap's reference width, x k
            const float width = 2.f * sigma * f / (zo - L);
            const bool ok = near(out[0].intensity[0], 4.f * weight, 2e-3f) && (q > 1.f ? near(width, cap, 2e-3f) && st.capped == 1 : st.capped == 0 && near(out[0].shape[1], val)) &&
                            st.faded == (q > .8f ? 1u : 0u) && near(out[0].local[3], .25f * out[0].shape[1]);
            char what[64]; std::snprintf(what, sizeof what, "near-camera cap q=%.1f", double(q));
            expect(ok, what);
            std::printf("CAP q=%.2f weight=%.3f width_px=%.2f cap_px=%.2f k=%.4f\n", double(q), double(out[0].intensity[0] / 4.f), double(width), double(cap), double(out[0].shape[1] / val));
        }
        // Side view: the length is free (a long plume across the screen keeps its length).
        const float val = 100.f / ppu(Z);
        const ee::Record lng = rec(0, 0, Z, -1, 0, 0, val, 9.f);
        build(&lng, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st, nullptr, &flat);
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
        expect(out[0].tint == pack_colour(m) && out[0].tint == 0xff3380ffu, "the body's mean");
        r.body = -1;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect(out[0].tint == pack_colour(cluster_tint(ee::red)) && out[0].tint == 0xffff1212u, "no body: the cluster's tint");
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
    // ----------------------------------------------------------- the stage's clock: an F8 capture's stall is no gap
    {
        const std::uint64_t hz = 1000000;
        StageClock c;
        c.step(5 * hz, 0, false);
        expect(!c.started && c.seconds == 0., "no frequency: no step");
        c.step(5 * hz, hz, false);
        expect(c.started && c.seconds == 0., "the first step starts the clock");
        std::uint64_t t = 5 * hz;
        for (unsigned i = 0; i < 10; ++i) c.step(t += hz / 60, hz, false);
        const double ordinary = c.seconds;
        expect(std::fabs(ordinary - 10. / 60.) < 1e-4, "ordinary steps: the counter");
        c.step(t += hz / 60, hz, true);       // the first capture frame
        c.step(t += 2 * hz, hz, true);        // the stall after it: one ordinary step
        c.step(t += 3 * hz, hz, false);       // the stall after the last capture frame: one ordinary step
        expect(std::fabs(c.seconds - ordinary - 3. / 60.) < 1e-4, "capture frames: the stall advances one ordinary step");
        const double before_gap = c.seconds;
        c.step(t += hz / 2, hz, false);       // a load screen: 0.5 s in full
        expect(std::fabs(c.seconds - before_gap - .5) < 1e-6, "an ordinary gap advances in full (the ribbons' 0.3 s rule)");
        c.step(t += 5 * hz, hz, true);        // a capture after the long gap: the ordinary step is held to 0.1 s
        expect(std::fabs(c.seconds - before_gap - .6) < 1e-6, "the capture step is held to 0.1 s");
        c.step(t - hz, hz, false);            // the counter went back: no step
        expect(std::fabs(c.seconds - before_gap - .6) < 1e-6, "a counter going back does not step");
        StageClock w; w.seconds = 1024. * 3 + 7.5; float wrapped = 0; w.wrapped(&wrapped);
        expect(wrapped == 7.5f, "the pixel program's clock wraps at 1,024 s");
    }
    // ----------------------------------------------------------- the pixel program's look constants
    {
        float c[20];
        pixel_constants(default_look, c);
        expect(near(c[0], .6f) && near(c[1], (.04f - .6f) * .45f) && near(c[3], 1.6f * .45f) && near(c[4], 1.05f) && near(c[5], .57f * .96f) &&
               near(c[6], 1.32f) && near(c[7], .4f) && near(c[8], .5f) && near(c[9], 6.2831853f / .16f) && near(c[10], 3.f) && near(c[11], .84f) &&
               near(c[12], .7f) && near(c[13], 1.f / .63f) && near(c[14], .552f) && near(c[16], 1.f / .54f) && near(c[17], .16f) && c[19] == 2.f,
               "look constants c3..c7 from the chosen settings");
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
            for (unsigned k = 0; k < 20; ++k) drawn = build(rs.data(), count, nullptr, v, Preset::standard, float(rep * 20 + k) / 60.f, vb.data(), count, nullptr);
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
        # The plume look (2026-10-03): the stage's clock steps once per stage run, held on F8 capture frames; the ribbons
        # take the same clock; the census writes engine_draw rows on capture frames too.
        self.assertIn('engine_clock_.step(std::uint64_t(counter.QuadPart),engine_qpc_frequency(),capture_);'
                      'engine_clock_.wrapped(&in.seconds);', inc)
        self.assertIn('f.seconds=engine_clock_.seconds;', source_text(ROOT / 'src/proxy/motion_output_engine_ribbons_inc.h'))
        self.assertIn('if(engine_row_frames_>=engine_row_frame_cap&&!capture_){',
                      source_text(ROOT / 'src/proxy/motion_output_engine_effects_inc.h'))
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
        # The plume look (2026-10-03): alive, bulge and taper, shock cells at both sizes.
        for case in ('temporal', 'shape', 'shock'):
            self.assertEqual(len(r['report'][case]), 2, case)
        self.assertTrue(all(.05 <= x['raw_cv'] <= .4 and abs(x['mean_over_design'] - 1) <= .1 for x in r['report']['temporal']))
        self.assertTrue(all(x['half_width_u01_over_nozzle_half'] >= 1.05 and x['half_width_u09_over_nozzle_half'] <= .35
                            for x in r['report']['shape']))
        self.assertTrue(all(x['still_maxima'] >= 3 for x in r['report']['shock']))

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
