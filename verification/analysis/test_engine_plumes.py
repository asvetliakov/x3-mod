"""Engine plumes, phase 2 (X3M_ENGINE_EFFECTS=plumes; docs/architecture/engine-effects-modern.md sections 3-6): the
portable core src/proxy/engine_plumes_core.h compiled on the host (the preset parser, the record -> vertex
builder: geometry, throttle law, presets, the screen minimums, the near-camera cap and fade, the RCS
puffs, the cull rules, capacity, tint and the body table's colours, the length pulse and its per-seed cache, the flow
phase, the nozzle parser, the build's cost), the preset's and the nozzle width's schema entries and launcher options, the wiring, and the tracked Wine record of
run_engine_plumes.py bound to its production sources.
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
    float sc[3]; for (unsigned i = 0; i < 3; ++i) preset_scale(Preset(i), &sc[i]);
    expect(sc[0] == .6f && sc[1] == 1.f && sc[2] == 1.5f, "scales 0.6 / 1 / 1.5");
    // ----------------------------------------------------------- indices
    {
        std::uint16_t ix[24]; write_indices(ix, 2);
        const std::uint16_t want[24] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7, 8, 9, 10, 10, 9, 11, 12, 13, 14, 14, 13, 15};
        expect(!std::memcmp(ix, want, sizeof ix), "two quads per nozzle, two triangles per quad");
    }
    const View v = view();
    const float Z = 2000.f, V = 50.f / ppu(Z); // the nozzle 25 px at the default 0.5, under the near fade band
    // The single law's detail level at that 25 px nozzle, smoothstep(8, 40, 25) = 0.596: the halo's e-fold
    // (0.5 + (0.32 - 0.5) detail) x halo 1.1 nozzle widths and the disc's halo gain 3 + (1.0 - 3) detail (2.82 at 1 until Run 128); DL15 at the
    // 15 px nozzle of the law rows (0.123).
    float DL = 0.f, DL15 = 0.f;
    law::smooth(8.f, 40.f, 25.f, &DL);
    law::smooth(8.f, 40.f, 15.f, &DL15);
    const float SG = (.5f + (.32f - .5f) * DL) * 1.1f, DH = 3.f + (1.f - 3.f) * DL, SG15 = (.5f + (.32f - .5f) * DL15) * 1.1f;
    // The disc's kappa by the detail level (Look::disc_kappa_smooth 1.84 -> disc_kappa 2.2) from a vertex's 8-bit detail
    // level (the 25 px nozzle's exact KA in the head-on block).
    auto kap = [](const Vertex& vx) { return 1.84f + (3.f - 1.84f) * float(vx.tint >> 24) / 255.f; };
    std::vector<Vertex> out(8 * 16);
    BuildStats st{};
    // ----------------------------------------------------------- side view: the axial quad (no length pulse)
    Look flat = default_look;
    flat.pulse = 0.f;
    flat.disc_far_low = 1.f; // the disc's distance law (after Run 127) off for the pins below; its own block tests it
    // After flight E: the halo brightness 0.20, and the distance law's factor at the small nozzles below (10 px, 4 px).
    const float HB = default_look.hb;
    // I(s) = lerp(IL, IH, s) (1.2 / 4.0), the heat 0.7, head_min 0.75 (the flight-F 1.248 / 4.16, 0.1, 0.42 reverted
    // after Run 126 A); the ring 0.3 and the mouth dip 0.5 over 0.3 L.
    const float IL = default_look.core_low, IH = default_look.core_high, RG = default_look.ring, HT = default_look.heat,
                DIP = default_look.mouth_dip, RAMP = default_look.mouth_ramp;
    expect(IL == 1.2f && IH == 4.f && RG == .3f && HT == .7f && DIP == .5f && RAMP == .3f && head_min == .75f,
           "Run123 look (flight-F mouth change reverted after Run 126 A): I(s) 1.2 .. 4.0, heat 0.7, head_min 0.75; ring 0.3, mouth dip 0.5 over 0.3 L");
    float fw10 = 0.f, fw4 = 0.f;
    distance_weight(default_look, 10.f, &fw10);
    distance_weight(default_look, 4.f, &fw4);
    expect(HB == .20f && near(fw10, .15f + .85f * .896f) && near(fw4, .15f + .85f * .104f), "hb 0.20; the distance law at 10 and 4 px");
    {
        // The distance law: 0.15 at and below 2 px, smoothstep to 1 at 12 px (0.4492 at 6), 1 above; a 6 px nozzle's
        // vertices scale by it (core, halo, the disc's terms) and count far_nozzles; a 12 px one does not.
        float w[6];
        const float at[6] = {0.f, 1.f, 2.f, 6.f, 12.f, 40.f};
        for (unsigned i = 0; i < 6; ++i) distance_weight(default_look, at[i], &w[i]);
        expect(near(w[0], .15f) && near(w[1], .15f) && near(w[2], .15f) && near(w[3], .15f + .85f * .352f) && w[4] == 1.f && w[5] == 1.f,
               "distance law: 0.15 at <= 2 px, 0.4492 at 6, 1 at 12 and 40");
        Look off = flat;
        off.far_low = 1.f;
        const ee::Record six = rec(0, 0, Z, 0, 0, -1, 12.f / ppu(Z), 2.f), twelve = rec(0, 0, Z, 0, 0, -1, 24.f / ppu(Z), 2.f);
        std::vector<Vertex> a(8), b(8);
        BuildStats sa{}, sb{};
        build(&six, 1, nullptr, v, Preset::standard, 0.f, a.data(), 1, &sa, nullptr, &flat);
        build(&six, 1, nullptr, v, Preset::standard, 0.f, b.data(), 1, &sb, nullptr, &off);
        bool scaled = sa.far_nozzles == 1 && sb.far_nozzles == 1;
        for (unsigned c = 0; c < 8; ++c) // the axial quad's intensity[2] is the axis's view z, the disc's the soft cap
            for (unsigned j = 0; j < (c < 4 ? 2u : 3u); ++j) scaled = scaled && near(a[c].intensity[j], b[c].intensity[j] * w[3], 1e-4f);
        scaled = scaled && near(a[4].local[2], b[4].local[2] * w[3], 1e-4f) && a[0].params == b[0].params;
        build(&twelve, 1, nullptr, v, Preset::standard, 0.f, a.data(), 1, &sa, nullptr, &flat);
        build(&twelve, 1, nullptr, v, Preset::standard, 0.f, b.data(), 1, &sb, nullptr, &off);
        scaled = scaled && sa.far_nozzles == 0 && near(a[0].intensity[0], b[0].intensity[0]) && near(a[4].intensity[0], b[4].intensity[0]);
        expect(scaled, "distance law: a 6 px nozzle's core, halo, disc and ring at 0.4492 (far_nozzles 1), a 12 px one whole");
    }
    {
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        const unsigned n = build(&r, 1, nullptr, v, Preset::standard, 5.f, out.data(), 16, &st, nullptr, &flat);
        expect(n == 1 && st.nozzles == 1 && st.vertices == 8 && st.discs == 0 && st.capped == 0 && st.faded == 0, "side view: one nozzle, no disc");
        const float L = 2.f * V, nw = .5f * V, px = 1.f / ppu(Z);
        bool layout = true, plane = true;
        const float a[3] = {-1, 0, 0};
        float nrm[3] = {out[1].position[0] - out[0].position[0], out[1].position[1] - out[0].position[1], out[1].position[2] - out[0].position[2]};
        const float nl = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        for (float& c : nrm) c /= nl;
        plane = std::fabs(nrm[0] * a[0] + nrm[1] * a[1] + nrm[2] * a[2]) < 1e-5f && std::fabs(nrm[2]) < 1e-3f; // across is perpendicular to the axis and to the line of sight
        for (unsigned c = 0; c < 4; ++c) {
            const Vertex& x = out[c];
            for (unsigned j = 0; j < 3; ++j) layout = layout && near(x.position[j], r.origin[j] + a[j] * x.local[0] + nrm[j] * x.local[1], 1e-4f);
            layout = layout && near(x.local[2], L) && near(x.local[3], nw) && near(x.shape[0], SG) && near(x.shape[1], V) && x.shape[2] == 0.f && x.shape[3] == 0.f;
        }
        expect(layout && plane, "axial quad: position = origin + axis x + side y, L = z value, n = value / 2 (the default nozzle 0.5), halo sigma0 by the detail level (0.55 -> 0.352 nozzle widths), kind 0");
        // The body's eroded edge (1 + 0.7 erode (0.6 + 0.8 u) of w(u): the revised law's streak erosion at S2's minimum),
        // the halo window (2.25 sigma0, the nozzle's sigma along the
        // whole plume as in the mock-up; half discs behind the nozzle and past the tip) and the ring (u near 0) inside the
        // trapezoid, linear in x. The mock-up's width: bulge 1.15, taper 0.45, tail narrowing 0.6.
        auto ss = [](float e0, float e1, float x) { float t = (x - e0) / (e1 - e0); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); };
        auto w_of = [&](float u) {
            const float b = .575f * (1 - .55f * std::exp(-9 * u)) * (1 + .35f * ss(0, .25f, u) * std::exp(-4 * u));
            const float line = .575f + (.04f - .575f) * .45f * u;
            return (line < b + .575f * .45f ? line : b + .575f * .45f) * std::max(1 - .6f * .45f * ss(.6f, 1, u), .05f);
        };
        const float x0 = out[0].local[0], x1 = out[2].local[0], h0 = std::fabs(out[0].local[1]), h1 = std::fabs(out[2].local[1]);
        bool covered = true;
        float worst = 1e9f;
        for (float x = x0; x <= x1; x += (x1 - x0) / 512.f) {
            const float u = x / L, uc = u < 0 ? 0 : u > 1 ? 1 : u, w = w_of(uc), reach = 2.25f * SG * nw;
            float need = 0;
            if (u >= 0 && u <= 1) need = std::max((1.2f + .7f * .57f * (.6f + .8f * u)) * w * nw, reach); // the outer sheath's edge at 1.2
            else {
                const float d = u < 0 ? -x : x - L;
                need = d < reach ? std::sqrt(reach * reach - d * d) : 0.f;
            }
            if (x >= -.05f * nw && x <= .3f * nw) need = std::max(need, (.46f * 1.15f + 3.f * .0645497f) * nw);
            const float have = h0 + (h1 - h0) * (x - x0) / (x1 - x0);
            covered = covered && need <= have + 1e-3f;
            worst = std::min(worst, have - need);
        }
        expect(covered && x0 <= -2.25f * SG * nw && x1 >= L, "the trapezoid covers the body, the halo window and the ring");
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
        expect(near(out[0].intensity[0], IH) && near(out[0].intensity[1], HB) && out[0].intensity[2] == 0.f &&
               out[0].intensity[3] == Z && out[4].intensity[3] == Z, "I_core core_high, I_halo 0.20 (hb x lerp(0.3, 1, 1)) at s 1; the axis's view z; the nozzle's view z");
        expect((out[0].tint & 0xffffffu) == 0xffffffu, "cluster white: neutral tint");
        expect((out[0].params >> 24) == 0u && (out[0].fog >> 24) == 255u && ((out[0].params >> 16) & 255u) == 255u && ((out[0].params >> 8) & 255u) == seed_byte(r) &&
               (out[0].params & 255u) == unsigned(int(RG / IH * .5f * 255.f + .5f)), "params: s 1, the seed, I_ring / I_core / 2 (ring / core_high), no disc weight; fog A sin(view) 1");
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
    // ----------------------------------------------------------- throttle law and presets (value 30 px: strong's
    // drawn width 2 x 2.25 x 0.825 x 15 px = 56 px stays under the fade band of the 0.12 H cap; a 15 px nozzle is at the
    // detail level 0: the previous law's halo e-fold 0.5 halo)
    for (const float zs : {.25f, 1.125f, 2.f})
        for (unsigned pr = 0; pr < 3; ++pr) {
            const float V = 30.f / ppu(Z);
            const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, zs);
            build(&r, 1, nullptr, v, Preset(pr), 9.f, out.data(), 16, &st);
            const float s = r.s;
            float pulse = 0;
            length_pulse(default_look, seed_byte(r), 9.f, &pulse);
            const bool ok = near(out[0].local[2], std::max(zs, .5f) * V * pulse) && near(out[0].shape[0], SG15 * sc[pr]) && // idle floor 0.5
                            near(out[0].intensity[0], (IL + (IH - IL) * s) * sc[pr]) && near(out[0].intensity[1], HB * (IL + (IH - IL) * s) / IH * sc[pr]);
            char what[96]; std::snprintf(what, sizeof what, "law z=%.3f preset=%s", double(zs), preset_name(Preset(pr)));
            expect(ok, what);
        }
    // ----------------------------------------------------------- head-on and tail-on: the end-on disc and the bias
    {
        const ee::Record away = rec(0, 0, Z, 0, 0, 1, V, 2.f), at = rec(0, 0, Z, 0, 0, -1, V, 2.f);
        build(&away, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st, nullptr, &flat);
        // The disc's half: the body's eroded edge at the along-mean growth (1 + 0.7 erode of w <= 0.575), the end-on ring
        // (its radius plus three of its sigmas, twice the side's since the tuning pass), the halo's reach; all at the disc's
        // nozzle width, the natural one x disc_radius DR (0.5 since Run 128).
        const float nw = .5f * V, DR = .5f, half = std::max(std::max((1.2f + .7f * .57f) * .575f, .46f * 1.15f + 3.f * .0645497f), 2.25f * SG) * DR * nw + 1.f / ppu(Z);
        expect(default_look.disc_radius == DR, "the end-on disc's radius 0.5 x the nozzle width (after Run 128)");
        const float Ln = 2.f * V / nw; // L / n = 4 at full throttle
        // The side view's peak per I_core on the axis (after flight D, with the mouth ramp; the revised law's carving
        // cells, the cooling core): max over u of (1 + 0.6 (1 - 0.5 smoothstep(0.2, 0.8, u))) x tail(u) x ramp(u) x
        // (1 - s carve(u)), carve = 0.85 exp(-3 u) smoothstep(0, 0.08, u)
        // (1 - ((1 + cos(2 pi u / 0.16)) / 2)^3), an independent replica (std::exp / std::cos) on 4,097 samples.
        auto ss_ = [](float e0, float e1, float x) { float q = (x - e0) / (e1 - e0); q = q < 0 ? 0 : q > 1 ? 1 : q; return q * q * (3 - 2 * q); };
        auto axis_at = [&](float s_) {
            float best = 0.f;
            for (unsigned i = 0; i <= 4096; ++i) {
                const float u = float(i) / 4096.f;
                const float tl = (1 - ss_(.4f, 1, u)) * std::exp(-.84f * u) * (1 - DIP * (1 - ss_(0, RAMP, u)));
                const float cr = .5f + .5f * std::cos(6.2831853f * u / .16f);
                const float cl = .85f * std::exp(-3.f * u) * ss_(0, .08f, u) * (1 - cr * cr * cr);
                best = std::max(best, (1 + .6f * (1 - .5f * ss_(.2f, .8f, u))) * tl * (1 - s_ * cl));
            }
            return best;
        };
        auto previous_at = [&](float) { // the smooth law's (detail 0, no cells): max of the cooling core's boost x tail
            float best = 0.f;
            for (unsigned i = 0; i <= 4096; ++i) {
                const float u = float(i) / 4096.f;
                const float tl = (1 - ss_(.4f, 1, u)) * std::exp(-.84f * u) * (1 - DIP * (1 - ss_(0, RAMP, u)));
                best = std::max(best, (1 + .6f * (1 - .5f * ss_(.2f, .8f, u))) * tl);
            }
            return best;
        };
        const float axis_peak = axis_at(1.f), axis_peak0 = axis_at(0.f);
        // The disc's cap at the 25 px nozzle's detail level DL: the side peak with the cells x DL (s 1); kappa 1.84 -> 8.2
        // by DL; the end-on ring x (1 + (3 min(1, (L / n) / 2) - 1) DL), L / n 4; the soft cap 1.0 x the side peak (both
        // since Run 125: x 8 and 1.5 before).
        const float cap_peak = axis_at(DL), KA = 1.84f + (3.f - 1.84f) * DL;
        bool disc = st.discs == 1 && (out[4].peak >> 24) == 255u && (out[0].peak >> 24) == 127u && near(out[4].local[3], DR * nw) && near(out[4].intensity[0], IH * KA * Ln, 2e-3f) &&
                    near(out[4].intensity[1], HB * DH * Ln) && near(out[4].intensity[2], 1.f * IH * cap_peak, 1e-2f) &&
                    near(out[4].local[2], IH * (RG / IH * .5f) * 2.f * (1.f + (3.f - 1.f) * DL), 2e-3f);
        for (unsigned c = 4; c < 8; ++c) disc = disc && near(std::fabs(out[c].position[0]), half, 1e-3f) && near(std::fabs(out[c].position[1]), half, 1e-3f) && out[c].position[2] == Z;
        bool finite = true;
        for (unsigned c = 0; c < 4; ++c) for (unsigned j = 0; j < 3; ++j) finite = finite && std::isfinite(out[c].position[j]);
        expect(disc && finite && out[0].shape[2] == 0.f && near(out[0].intensity[2], 1.f) && near(out[0].intensity[0], .5f * IH) && near(out[0].intensity[1], HB * .5f),
               "head-on: the end-on disc (I x (1.84 -> 3 by the detail) L / n, halo I_halo x (3 -> 1.0 by the detail) L / n, cap 1.0 x the side peak, the ring x 1 -> 3 by the detail), the axial quad at half weight, no bias");
        float pk = 0.f;
        LookTables tb;
        look_tables(flat, &tb);
        peak_axis_at(tb, 1.f, &pk);
        float pk0 = 0.f;
        peak_axis_at(tb, 0.f, &pk0);
        float pkp = 0.f;
        peak_axis_at(tb, 1.f, &pkp, 0.f);
        expect(near(pk, axis_peak, 2e-3f) && near(pk0, axis_peak0, 2e-3f) && near(pkp, previous_at(1.f), 2e-3f),
               "the side view's axis peak with the mouth ramp, at s 1 and s 0; the smooth law's (no cells) at detail 0");
        std::printf("AXIS_PEAK s1=%.5f s0=%.5f replica_s1=%.5f replica_s0=%.5f\n", double(pk), double(pk0), double(axis_peak), double(axis_peak0));
        build(&at, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat);
        expect(near(out[0].shape[2], .5f * V) && near(out[4].shape[2], .5f * V) && near(out[0].intensity[2], -1.f), "tail-on: the occlusion bias 0.5 value");
        // Facing f = |axis . to_camera|: the disc smoothstep(0.3, 0.7, f), the view factor f; the axial 1 - 0.5 x the disc's weight.
        struct Case { float ax, az, wd; const char* what; };
        const Case cases[] = {{-.5f, .8660254f, 1.f, "facing 0.866 (30 degrees off the axis): the full disc, view factor 0.866"},
                              {-.8660254f, .5f, .5f, "facing 0.5 (60 degrees): half the disc, the axial 0.75"},
                              {-.9539392f, .3f, 0.f, "facing 0.3: no disc, the axial whole"},
                              {-.9797959f, .2f, 0.f, "facing 0.2: no disc"}};
        for (const Case& c : cases) {
            const ee::Record r = rec(0, 0, Z, c.ax, 0, c.az, V, 2.f);
            build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat);
            const float f = c.az, wa = 1.f - .5f * c.wd;
            bool ok = near(out[0].intensity[0], IH * wa, 1e-3f) && near(out[0].intensity[1], HB * wa, 1e-3f);
            if (c.wd > 0.f)
                ok = ok && st.discs == 1 && near(out[4].intensity[0], c.wd * IH * KA * Ln * f, 1e-3f) &&
                     near(out[4].intensity[1], c.wd * HB * DH * Ln * f, 1e-3f) && near(out[4].intensity[2], c.wd * 1.f * IH * cap_peak, 1e-2f);
            else
                ok = ok && st.discs == 0 && out[4].position[0] == out[5].position[0] && out[4].position[1] == out[6].position[1];
            expect(ok, c.what);
        }
        {
            const ee::Record r = rec(0, 0, Z, -.8660254f, 0, .5f, V, 2.f);
            build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat);
            expect((out[0].params >> 24) == 128u && (out[4].params >> 24) == 128u && (out[0].fog >> 24) == unsigned(int(.8660254f * 255.f + .5f)),
                   "the mouth hand-over: params A the disc weight 0.5, fog A sin(view) 0.866");
        }
        float wd = 0.f, wa = 0.f;
        facing_weights(default_look, .5f, &wd, &wa);
        expect(near(wd, .5f) && near(wa, .75f), "facing weights at 0.5");
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
        expect(build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st) == 1 && near(out[0].local[3] * ppu(zf), 2.f) &&
               near(out[0].local[2] * ppu(zf), 4.f) && near(out[4].local[3] * ppu(zf), 2.f) && near(out[0].intensity[0], IL * .15f) &&
                   st.far_nozzles == 1, "minimums (the dot floor after flight E): nozzle width 2 px, length 4 px; a 2 px nozzle at 0.15 of the radiance");
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
               near(out[0].intensity[0], IL * .1f * fw10) && near(out[0].intensity[1], HB * .3f * .1f * fw10), "RCS: L = z value x the pulse, unlengthened, radiance x z (x the distance law at its 10 px nozzle)");
    }
    // ----------------------------------------------------------- the near-camera cap and fade
    {
        const float f = 1.7f * 540.f, cap = .12f * 1080.f;
        // Tail-on at the nozzle depth zo, L = 2 value: the body width 2 h value at the tip (zo - L) over the cap is q,
        // h the body's half-width per value: its eroded edge 1.2736 x 0.575 nozzle widths (wider than the ring's 0.7226;
        // the halo's reach does not count, after the review of flight C; unchanged by the revised law) x the nozzle width
        // 0.5. Tail-on the axial quad takes half (the end-on disc is whole); the fade takes the axial quad to 0.5, the
        // disc to 0.4 (chase_disc_floor, its own fade over the same band since flight G; a floor of 0.6 before) of its
        // unfaded 4 x kappa(detail) x L / n (L / n = 4: the cap shrinks the body's L and n together). Since Run 125 the
        // end-on disc keeps the natural nozzle width (the body is shrunk as before), x disc_radius 0.5 since Run 128: its
        // nozzle width 0.25 value and its half-size at that width, the axial quad's n over the disc's n x disc_radius (k) in
        // the head colour's alpha x 127. The
        // disc's half-size: the end-on ring's radius plus three of its sigmas (twice the side's since the tuning pass),
        // 0.529 + 0.387, past the outer sheath's eroded edge at the along-mean growth, (1.2 + 0.7 x 0.57) x 0.575, and the halo's 2.25 x 0.352.
        const float h = (1.f + .48f * .57f) * .575f * .5f,
                    h_halo = std::max(std::max((1.2f + .7f * .57f) * .575f, 2.25f * .352f), .46f * 1.15f + 3.f * .0645497f) * .5f;
        auto at_q = [&](float q, float zo) { // value with 2 h value f / (zo - 2 value) = q cap
            return q * cap * zo / (2.f * h * f + 2.f * q * cap);
        };
        for (const float q : {.7f, .9f, 1.f, 3.f, 40.f}) {
            const float zo = 400.f, val = at_q(q, zo), pp = f / zo;
            const ee::Record r = rec(0, 0, zo, 0, 0, -1, val, 2.f);
            build(&r, 1, nullptr, v, Preset::standard, 4.f, out.data(), 16, &st, nullptr, &flat);
            const float weight = q <= .8f ? 1.f : q >= 1.f ? .5f : 1.f - .5f * (q - .8f) / .2f;
            const float disc_fade = q <= .8f ? 1.f : q >= 1.f ? .4f : 1.f - .6f * (q - .8f) / .2f;
            const float half = h * out[0].shape[1], L = out[0].local[2]; // the body's half-width, x k
            const float width = 2.f * half * f / (zo - L);
            const float quad_half = std::fabs(out[4].local[0]) - 1.f / pp; // the disc's half (width0) less its pixel
            const float disc = IH * kap(out[4]) * (L / out[0].local[3]) * disc_fade;
            const float kk = out[0].shape[1] / val;
            const bool ok = near(out[0].intensity[0], .5f * IH * weight, 2e-3f) && (q > 1.f ? near(width, cap, 2e-3f) && st.capped == 1 : q == 1.f ? near(width, cap, 2e-3f) && near(out[0].shape[1], val, 1e-3f) : st.capped == 0 && near(out[0].shape[1], val)) &&
                            st.faded == (q > .8f ? 1u : 0u) && near(out[0].local[3], .5f * out[0].shape[1]) &&
                            near(out[4].local[3], .25f * val) && near(out[4].shape[1], val) && near(quad_half, .5f * h_halo * val, 2e-3f) && st.discs_capped == 0 &&
                            (out[0].peak >> 24) == unsigned(int(std::min(kk, 1.f) * 127.f + .5f)) &&
                            near(out[4].intensity[0], disc, 2e-3f) &&
                            out[4].intensity[0] >= .4f * IH * kap(out[4]) * (L / out[0].local[3]) * (1.f - 2e-3f);
            char what[64]; std::snprintf(what, sizeof what, "near-camera cap q=%.1f", double(q));
            expect(ok, what);
            std::printf("CAP q=%.2f weight=%.3f disc_weight=%.3f width_px=%.2f cap_px=%.2f k=%.4f disc_n_px=%.2f handover_alpha=%u\n", double(q),
                        double(out[0].intensity[0] / (.5f * IH)), double(out[4].intensity[0] / (IH * kap(out[4]) * (L / out[0].local[3]))),
                        double(width), double(cap), double(kk), double(out[4].local[3] * pp), unsigned(out[0].peak >> 24));
        }
        // The disc's own cap: a nozzle whose disc half-size (at disc_radius 0.5) projects to twice 0.35 H draws its disc at
        // 0.35 H (its quad and nozzle width scaled together), the axial quad shrunk by k as before.
        {
            const float zo = 400.f, pp = f / zo, val = 2.f * .35f * 1080.f / (.5f * h_halo * pp);
            const ee::Record r = rec(0, 0, zo, 0, 0, -1, val, 2.f);
            build(&r, 1, nullptr, v, Preset::standard, 4.f, out.data(), 16, &st, nullptr, &flat);
            const float scale = .35f * 1080.f / (.5f * h_halo * val * pp + 1.f);
            expect(st.discs_capped == 1 && st.capped == 1 && near(std::fabs(out[4].local[0]) * pp, .35f * 1080.f, 2e-3f) &&
                       near(out[4].local[3], .25f * val * scale, 2e-3f) && out[0].shape[1] < val,
                   "the disc's projected radius held to 0.35 H (disc and its nozzle width scaled; the axial quad shrunk by k)");
        }
        // Side view: the length is free (a long plume across the screen keeps its length).
        const float val = 100.f / ppu(Z);
        const ee::Record lng = rec(0, 0, Z, -1, 0, 0, val, 9.f);
        build(&lng, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st, nullptr, &flat);
        expect(st.capped == 0 && near(out[0].local[2], 9.f * val), "a long plume (brake flare 9 value) is not shortened");
    }
    // ----------------------------------------------------------- co-located layers (merge_layers, after flight G;
    // since Run 129 A the marked record is unfloored, not dropped)
    {
        // run412 frame 5437: the Scorpion's nor 10 and tiny 5.04, 6.9 units apart on one axis, one parent.
        const float ax = -.66242f, ay = .22177f, az = .71555f;
        ee::Record m[10];
        m[0] = rec(96411.5f, -37509.1f, 19210.f, ax, ay, az, 10.f, 2.f);
        m[1] = rec(96405.8f, -37505.3f, 19209.f, ax, ay, az, 5.04f, 2.f);
        m[2] = rec(0.f, 0.f, 500.f, 0, 0, -1, 10.f, 2.f);   // a twin of equal size 5 apart: kept
        m[3] = rec(5.f, 0.f, 500.f, 0, 0, -1, 10.f, 2.f);
        m[4] = rec(0.f, 0.f, 900.f, 0, 0, -1, 10.f, 2.f);   // a smaller one 3 nozzle widths (15) away: kept
        m[5] = rec(15.f, 0.f, 900.f, 0, 0, -1, 5.f, 2.f);
        m[6] = rec(0.f, 0.f, 1300.f, 0, 0, -1, 10.f, 2.f);  // anti-parallel, co-located: kept
        m[7] = rec(1.f, 0.f, 1300.f, 0, 0, 1, 5.f, 2.f);
        // run413: the Split Ocelot's huge 939 and a side nozzle big3 187.5 (ratio 0.2, 0.3 x 939 apart, parallel): a
        // nozzle of its own (under merge_size_min 0.35 since Run 125), floored.
        m[8] = rec(0.f, 0.f, 9000.f, 0, 0, -1, 939.f, 2.f);
        m[9] = rec(281.7f, 0.f, 9000.f, 0, 0, -1, 187.5f, 2.f);
        const std::uint32_t parents[10] = {7, 7, 9, 9, 11, 11, 13, 13, 15, 15};
        std::uint8_t drop[10];
        const unsigned unfloored = merge_layers(m, 10, parents, drop);
        expect(unfloored == 1 && drop[1] == 1 && !drop[0] && !drop[2] && !drop[3] && !drop[4] && !drop[5] && !drop[6] && !drop[7] && !drop[8] &&
                   !drop[9],
               "layers: the smaller parallel layer (ratio 0.35..0.75) within the larger's size is unfloored; twins, 3 widths "
               "apart, anti-parallel, a 0.2-ratio side nozzle floored");
        // Transitive: an inner layer at 0.25 of the outer (under the window) and 0.5 of a middle layer (0.5 of the outer)
        // is unfloored through the middle one, which the outer unfloors (an unfloored record's smaller layers are too).
        {
            ee::Record l3[3] = {rec(0.f, 0.f, 700.f, 0, 0, -1, 10.f, 2.f), rec(1.f, 0.f, 700.f, 0, 0, -1, 5.f, 2.f),
                                rec(2.f, 0.f, 700.f, 0, 0, -1, 2.5f, 2.f)};
            const std::uint32_t p3[3] = {21, 21, 21};
            std::uint8_t d3[3];
            expect(merge_layers(l3, 3, p3, d3) == 2 && !d3[0] && d3[1] && d3[2],
                   "layers are transitive: 0.25 of the outer is unfloored through a 0.5 middle layer");
        }
        // The near bound (merge_layer_near 1.5 x the smaller's size, after Run 129 A): the Split Raptor's big2 93.66 at
        // 170 and 196 units from its big3 187.5 (1.8 / 2.1 x the big2) are real nozzles; a 10 + 5 pair 1.4 x the smaller
        // apart is a layer, 1.6 x is not.
        {
            ee::Record r4[5] = {rec(57823.5f, 886.43f, 37895.8f, .66465f, -.64582f, -.37572f, 187.5f, 2.f),
                                rec(57708.9f, 764.35f, 37925.8f, .66465f, -.64582f, -.37572f, 93.66f, 2.f),
                                rec(57936.f, 1045.08f, 37872.1f, .66465f, -.64582f, -.37572f, 93.66f, 2.f),
                                rec(0.f, 0.f, 300.f, 0, 0, -1, 10.f, 2.f), rec(7.f, 0.f, 300.f, 0, 0, -1, 5.f, 2.f)};
            const std::uint32_t p4[5] = {31, 31, 31, 33, 33};
            std::uint8_t d4[5];
            expect(merge_layers(r4, 5, p4, d4) == 1 && !d4[0] && !d4[1] && !d4[2] && !d4[3] && d4[4],
                   "the near bound: the Raptor's big2s are nozzles (floored), a pair 1.4 x the smaller apart is a layer");
            r4[4].origin[0] = 8.f;
            expect(merge_layers(r4, 5, p4, d4) == 0 && !d4[4], "the near bound: 1.6 x the smaller apart is not a layer");
        }
        const std::uint32_t other[2] = {7, 8}, unknown[2] = {0, 0};
        expect(merge_layers(m, 2, other, drop) == 0 && !drop[1] && merge_layers(m, 2, unknown, drop) == 0 &&
                   merge_layers(m, 2, nullptr, drop) == 0, "layers: another parent, an unknown parent or none: floored");
        // The builder (after Run 129 A): both layers draw, the larger at its floored value, the smaller at its natural
        // one (R 67.3, floor scale 1: the nor 10 -> 23.5, the tiny stays 5.04 instead of 20.2); without parents both floor.
        {
            Look f1 = default_look;
            f1.floor_scale = 1.f;
            ee::Record pair[2] = {rec(0.f, 0.f, 400.f, 0, 0, -1, 10.f, 2.f), rec(-5.7f, 3.8f, 399.f, 0, 0, -1, 5.04f, 2.f)};
            const float rr[2] = {67.3f, 67.3f};
            const std::uint32_t pp[2] = {7, 7};
            std::vector<Vertex> vb(2 * vertices_per_nozzle);
            BuildStats st;
            const unsigned n = build(pair, 2, nullptr, v, Preset::standard, 0.f, vb.data(), 2, &st, nullptr, &f1, nullptr, rr,
                                     nullptr, pp);
            expect(n == 2 && st.unfloored == 1 && st.floored == 1 && std::fabs(vb[0].shape[1] - 23.555f) < .01f &&
                       near(vb[vertices_per_nozzle].shape[1], 5.04f),
                   "layers: the larger floored, the unfloored layer drawn at its natural value");
            build(pair, 2, nullptr, v, Preset::standard, 0.f, vb.data(), 2, &st, nullptr, &f1, nullptr, rr);
            expect(st.unfloored == 0 && st.floored == 2 && vb[vertices_per_nozzle].shape[1] > 20.f,
                   "layers: without parents both records floor");
        }
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
        expect((out[0].tint & 0xffffffu) == (pack_colour(m) & 0xffffffu) && (out[0].tint & 0xffffffu) == 0x3380ffu, "the body's mean");
        r.body = -1;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect((out[0].tint & 0xffffffu) == (pack_colour(cluster_tint(ee::red)) & 0xffffffu) && (out[0].tint & 0xffffffu) == 0xff1212u, "no body: the cluster's tint");
        b.colour = 0; r.body = 3;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect((out[0].tint & 0xffffffu) == (pack_colour(cluster_tint(ee::red)) & 0xffffffu), "a body without colours: the cluster's tint");
        // The revised law's detail level in the tint's alpha: smoothstep(16, 40) of the drawn nozzle width, the axial quad
        // and the disc alike (side view: 15, 28, 40 px nozzles; head-on 28 px).
        unsigned alpha[3] = {};
        const float sizes[3] = {30.f, 56.f, 80.f};
        for (unsigned i = 0; i < 3; ++i) {
            const ee::Record q = rec(0, 0, Z, -1, 0, 0, sizes[i] / ppu(Z), 2.f);
            build(&q, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
            alpha[i] = out[0].tint >> 24;
        }
        const ee::Record head_on = rec(0, 0, Z, 0, 0, -1, 56.f / ppu(Z), 2.f);
        build(&head_on, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st);
        expect(alpha[0] == 31u && alpha[1] == 174u && alpha[2] == 255u && (out[0].tint >> 24) == 174u && (out[4].tint >> 24) == 174u,
               "detail level smoothstep(8, 40): 0.123 at a 15 px nozzle, 0.684 at 28 px, 1 at 40 px; the disc's the same");
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
        StageClock s1;
        s1.step(hz, hz, false);
        expect(s1.last_step == 0., "the first step advances nothing");
        s1.step(hz + hz / 60, hz, false);
        expect(std::fabs(s1.last_step - 1. / 60.) < 1e-6, "last_step: the ordinary step");
        s1.step(hz + hz / 60 + 2 * hz, hz, true);
        expect(std::fabs(s1.last_step - 1. / 60.) < 1e-6, "last_step: a capture stall held to the ordinary step");
        s1.step(hz, hz, false);
        expect(s1.last_step == 0., "last_step: a counter going back advances nothing");
    }
    // ----------------------------------------------------------- the flow phase: constant speed in nozzle widths
    {
        float rate = 0;
        flow_rate(default_look, &rate);
        expect(near(rate, .35f * 3.f * 4.f / 1.6f), "flow rate 2.625 nozzle widths per second at the default nozzle 0.5 (the mock-up's speed in value units)");
        Look narrow = default_look; narrow.nozzle_width = .25f;
        float rate_narrow = 0;
        flow_rate(narrow, &rate_narrow);
        expect(near(rate_narrow * .25f, rate * .5f) && near(rate_narrow, 5.25f), "the same speed in value units at any nozzle width (5.25 at 0.25)");
        FlowPhase ph;
        for (unsigned i = 0; i < 600; ++i) ph.advance(1. / 60., rate);
        expect(std::fabs(ph.nozzle_widths - 26.25) < 1e-4, "600 frames at 60 fps: 10 s x 2.625");
        const double kept = ph.nozzle_widths;
        ph.advance(0., rate); ph.advance(-1., rate); ph.advance(1. / 60., -1.f); ph.advance(1e300, rate);
        expect(ph.nozzle_widths == kept, "no advance on a zero, negative or huge step or a non-positive rate");
        // Gap 4: the nozzle's share flow_reference / value in [0.3, 1]: one world speed from value 500 to 1,667.
        float f100 = 0, f500 = 0, f600 = 0, f1000 = 0, f1500 = 0, f1667 = 0, f1e4 = 0, f0 = 0, fnan = 0;
        flow_factor(default_look, 100.f, &f100); flow_factor(default_look, 500.f, &f500); flow_factor(default_look, 600.f, &f600);
        flow_factor(default_look, 1000.f, &f1000); flow_factor(default_look, 1500.f, &f1500); flow_factor(default_look, 1700.f, &f1667);
        flow_factor(default_look, 1e4f, &f1e4); flow_factor(default_look, 0.f, &f0); flow_factor(default_look, NAN, &fnan);
        expect(f100 == 1.f && f500 == 1.f && near(f1000, .5f) && near(f600, 500.f / 600.f) && near(f1500, 1.f / 3.f) && f1667 == .3f &&
               f1e4 == .3f && f0 == 1.f && fnan == 1.f, "flow factor: 1 up to value 500, 500 / value, at least 0.3");
        const float speed600 = rate * f600 * .5f * 600.f, speed1500 = rate * f1500 * .5f * 1500.f;
        expect(near(speed600, 656.25f, 1e-4f) && near(speed1500, 656.25f, 1e-4f) && near(rate * f100 * .5f * 100.f, 131.25f) &&
               near(rate * f1e4 * .5f * 1e4f, 3937.5f), "world speed 656.25 per second from value 500 to 1,667 (value 100: 131.25, 10,000: 3,937.5)");
        // The nozzle's phase over 10^4 s at 60 fps: continuous frame to frame except one step of -4,096 per wrap, in
        // [0, 4,096), and the wraps counted by flow x factor.
        {
            FlowPhase acc; float previous[2] = {0, 0}; unsigned wraps[2] = {0, 0}; bool steady = true, bounded = true;
            const float factors[2] = {1.f, .3f};
            for (unsigned i = 0; i < 600000; ++i) {
                acc.advance(1. / 60., rate);
                for (unsigned k = 0; k < 2; ++k) {
                    float p = 0; nozzle_phase(acc.nozzle_widths, factors[k], &p);
                    bounded = bounded && p >= 0.f && p < 4096.f;
                    const float step = p - previous[k], want = rate * factors[k] / 60.f;
                    if (step < 0.f) { ++wraps[k]; steady = steady && std::fabs(step + 4096.f - want) < 2e-3f; }
                    else steady = steady && std::fabs(step - want) < 2e-3f;
                    previous[k] = p;
                }
            }
            expect(steady && bounded && wraps[0] == unsigned(26250. / 4096.) && wraps[1] == unsigned(26250. * .3 / 4096.),
                   "phase over 10^4 s: steps rate x factor / 60 (within 2e-3), one wrap per 4,096 nozzle widths (6 and 1)");
            float big = 1.f; nozzle_phase(1e13, 1.f, &big);
            float neg = 1.f; nozzle_phase(-5., 1.f, &neg);
            expect(big == 0.f && neg == 0.f, "out of range or negative flow: phase 0");
        }
        // The vertex: both quads carry the nozzle's phase (shape.w); the kind is the head colour's alpha (0 axial, 255
        // disc). The phase follows the flow x the factor of the plume's own (floored, pre-cap) value, whatever the pulsed L.
        {
            const ee::Record r = rec(0, 0, Z, 0, 0, 1, V, 2.f); // head-on: the disc drawn
            Dynamics dy; dy.flow = 1234.5;
            build(&r, 1, nullptr, v, Preset::standard, 10.f, out.data(), 16, &st, nullptr, nullptr, nullptr, nullptr, &dy);
            float want = 0, fv = 0; flow_factor(default_look, V, &fv); nozzle_phase(1234.5, fv, &want);
            bool same = true;
            for (unsigned c = 0; c < 8; ++c) same = same && out[c].shape[3] == want;
            expect(same && st.discs == 1 && (out[0].peak >> 24) == 127u && (out[4].peak >> 24) == 255u, "both quads carry the nozzle's phase; the kind in the head colour's alpha (axial: the hand-over scale 1 = 127)");
            const ee::Record side = rec(0, 0, Z, -1, 0, 0, V, 2.f);
            Dynamics da; da.flow = 10.; Dynamics db; db.flow = 10. + rate / 60.;
            build(&side, 1, nullptr, v, Preset::standard, 10.f, out.data(), 16, &st, nullptr, nullptr, nullptr, nullptr, &da);
            const float La = out[0].local[2], a = out[0].shape[3];
            build(&side, 1, nullptr, v, Preset::standard, 10.f + 1.f / 60.f, out.data(), 16, &st, nullptr, nullptr, nullptr, nullptr, &db);
            const float Lb = out[0].local[2], b = out[0].shape[3];
            expect(La != Lb && near(b - a, rate * fv / 60.f, 1e-3f), "one frame's phase step is rate x factor / 60 while L pulses");
            const ee::Record big = rec(0, 0, Z * 50.f, -1, 0, 0, 1e4f, 2.f);
            build(&big, 1, nullptr, v, Preset::standard, 10.f, out.data(), 16, &st, nullptr, nullptr, nullptr, nullptr, &da);
            float wb = 0; nozzle_phase(10., .3f, &wb);
            expect(near(out[0].shape[3], wb), "value 10,000: phase = flow x 0.3");
        }
    }
    // ----------------------------------------------------------- the per-nozzle phase (review P1)
    // A nozzle's phase in the memory advances by the accumulator's step x this frame's factor: a value that doubles
    // between frames (factor 0.5 -> 0.3) changes the speed, not the position; a new key starts at the shared phase; a
    // factor change is counted; the read-only lookup (the shimmer's) gives the same phase; without the memory the shared
    // phase jumps by flow x the factor's change.
    {
        float rate = 0;
        flow_rate(default_look, &rate);
        static Transients mem; mem.clear();
        const double flow0 = 9450.; // an hour at 2.625 nozzle widths per second
        const ee::Record a = rec(0, 0, Z * 20.f, -1, 0, 0, 1000.f, 2.f), b = rec(0, 0, Z * 40.f, -1, 0, 0, 2000.f, 2.f);
        float phases[6]; unsigned changes = 0;
        for (unsigned i = 0; i < 6; ++i) {
            Dynamics dy; dy.transients = &mem; dy.step = 1.f / 60.f; dy.game_ms = 1000.f / 60.f; dy.flow = flow0 + double(i) * rate / 60.;
            build(i & 1u ? &b : &a, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat, nullptr, nullptr, &dy);
            phases[i] = out[0].shape[3];
            changes += st.flow_factor_changes;
        }
        float start = 0; nozzle_phase(flow0, .5f, &start);
        bool steady = near(phases[0], start, 1e-6f);
        for (unsigned i = 1; i < 6; ++i) {
            const float want = rate / 60.f * (i & 1u ? .3f : .5f);
            float step = phases[i] - phases[i - 1];
            if (step < 0.f) step += 4096.f;
            steady = steady && std::fabs(step - want) < 2e-3f;
        }
        float shared = 0; nozzle_phase(flow0 + rate / 60., .3f, &shared);
        float looked = -1;
        const bool found = mem.phase_of(identity_key(b), flow0 + 5. * rate / 60., .3f, &looked);
        expect(steady && changes == 5 && found && near(looked, phases[5], 1e-6f) && std::fabs(shared - phases[1]) > 100.f,
               "keyed phase: the first frame at flow x factor, each step rate x this frame's factor / 60 while the value flips (5 counted); the shared phase would jump");
        std::printf("FLOW_KEYED_HOST step_main=%.5f step_doubled=%.5f shared_jump=%.1f changes=%u\n", double(phases[2] - phases[1]),
                    double(phases[1] - phases[0]), double(std::fabs(shared - phases[1])), changes);
        // Without a slot (key 0 cannot occur; a full window): the shared phase, counted overflow.
        static Transients full; full.clear(); full.begin(1.f / 60.f);
        for (auto& sl : full.slots) { sl.key = 0x7fffffffffffull; sl.idle = 0.f; }
        Dynamics df; df.transients = &full; df.step = 0.f; df.flow = 1234.5;
        build(&a, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat, nullptr, nullptr, &df);
        float want = 0; nozzle_phase(1234.5, .5f, &want);
        expect(near(out[0].shape[3], want) && st.transient_overflow == 1, "a full window: the shared phase, one overflow");
    }
    // ----------------------------------------------------------- gap 5: two-tone colour
    {
        ee::Body b{}; b.colour = 1; const float m[3] = {1.f, .15f, .15f}, pk[3] = {1.f, .81f, .81f};
        std::memcpy(b.mean, m, sizeof m); std::memcpy(b.peak, pk, sizeof pk); g_body = &b;
        ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f, unsigned(ee::red) << ee::cluster_shift); r.body = 3;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        float head[3]; head_colour(m, pk, head);
        const float lm = .2126f + .7152f * .15f + .0722f * .15f, lp = .2126f + .7874f * .81f;
        expect(lm / lp < head_min && near(head[0], head_min) && near(head[1] / head[0], .81f) && (out[0].peak & 0xffffffu) == (pack_colour(head) & 0xffffffu) &&
               (out[0].tint & 0xffffffu) == (pack_colour(m) & 0xffffffu), "head colour: the peak's chroma, its scale to the mean's luminance (red: 0.389) held at head_min 0.75 (the revised law; flight F's 0.42 reverted), the tint the mean");
        const float cy[3] = {.14f, .71f, 1.f}, cyp[3] = {.27f, .9f, 1.f};
        float hc[3]; head_colour(cy, cyp, hc);
        const float lc = .2126f * .14f + .7152f * .71f + .0722f, lcp = .2126f * .27f + .7152f * .9f + .0722f;
        expect(lc / lcp > head_min && near(hc[1], .9f * lc / lcp), "head colour: a scale above head_min (cyan: 0.79) to the mean's luminance as before");
        const float dim[3] = {.2f, .2f, 1.f}; float same[3]; head_colour(m, dim, same);
        expect(same[0] == .2f && same[2] == 1.f, "a peak no brighter than the mean is kept");
        r.body = -1;
        build(&r, 1, &lookup, v, Preset::standard, 0, out.data(), 16, &st);
        expect((out[0].peak & 0xffffffu) == (out[0].tint & 0xffffffu), "no body colours: head = tail = the cluster's tint");
        g_body = nullptr;
    }
    // ----------------------------------------------------------- gap 10: the idle floor
    {
        const ee::Record idle = rec(0, 0, Z, -1, 0, 0, V, .25f), cruise = rec(0, 0, Z, -1, 0, 0, V, 1.f);
        build(&idle, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat);
        const float Li = out[0].local[2];
        build(&cruise, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat);
        const float Lc = out[0].local[2];
        const ee::Record puff = rec(0, 0, Z, -1, 0, 0, V, .1f, (unsigned(ee::white) << ee::cluster_shift) | ee::flag_steering);
        build(&puff, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat);
        const float Lp = out[0].local[2];
        expect(near(Li, .5f * V) && near(Lc, 1.f * V) && near(Lp, .1f * V) && near(out[0].intensity[0], IL * .1f),
               "idle floor: a main jet at z 0.25 is 0.5 value long (z 1: 1 value); an RCS jet keeps z value and its weight z");
    }
    // ----------------------------------------------------------- gap 6: the RCS puff attack and retro flare
    {
        const unsigned steer = (unsigned(ee::white) << ee::cluster_shift) | ee::flag_steering;
        auto run = [&](unsigned flags, const float* zs, unsigned frames, float* intensity) {
            static Transients mem; mem.clear();
            for (unsigned i = 0; i < frames; ++i) {
                ee::Record q = rec(0, 0, Z, -1, 0, 0, V, zs[i], flags); q.serial = 99; q.flags |= ee::flag_serial;
                if (zs[i] > 2.f + 1e-3f) q.flags |= ee::flag_brake;
                Dynamics dy; dy.transients = &mem; dy.step = 1.f / 60.f; dy.game_ms = 1000.f / 60.f;
                intensity[i] = build(&q, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat, nullptr, nullptr, &dy) ? out[0].intensity[0] : 0.f;
            }
        };
        float zs[16], in[16];
        zs[0] = .01f; zs[1] = .505f; for (unsigned i = 2; i < 16; ++i) zs[i] = 1.f;
        run(steer, zs, 16, in);
        const float steady = in[15];
        unsigned back = 0; for (unsigned i = 2; i < 16; ++i) if (in[i] > steady * 1.0001f) back = i;
        auto plain = [&](float z, unsigned flags) {
            const ee::Record q = rec(0, 0, Z, -1, 0, 0, V, z, flags);
            return build(&q, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat) ? out[0].intensity[0] : 0.f;
        };
        expect(in[0] == 0.f && near(in[2] / steady, 1.5f) && near(in[1] / plain(.505f, steer), 1.5f) && back == 9 && in[3] < in[2] &&
                   near(in[3] / steady, 1.f + .5f * (1.f - 1.f / 60.f / .12f)),
               "RCS puff z 0.01 -> 1 over three frames: 1.5 x steady at frame 2, linear decay over 120 ms (last boosted frame 9, 117 ms)");
        std::printf("ATTACK steering ratio_f1=%.4f ratio_f2=%.4f ratio_f3=%.4f last_boosted_frame=%u\n", double(in[1] / steady), double(in[2] / steady), double(in[3] / steady), back);
        float bz[16], bi[16];
        bz[0] = 2.2f; bz[1] = 2.6f; for (unsigned i = 2; i < 16; ++i) bz[i] = 3.f;
        run(unsigned(ee::white) << ee::cluster_shift, bz, 16, bi);
        expect(near(bi[2] / bi[15], 1.5f) && near(bi[1] / bi[15], 1.5f) && bi[0] == bi[15], "brake body z 2.2 -> 3: the same flash");
        float mz[4] = {.25f, 1.f, 2.f, 2.f}, mi[4];
        run(unsigned(ee::white) << ee::cluster_shift, mz, 4, mi);
        expect(near(mi[2], mi[3]) && near(mi[2], IH), "a main jet's rising z: no attack");
        float slow[16], si[16];
        for (unsigned i = 0; i < 16; ++i) slow[i] = .1f + .004f * 1000.f / 60.f * .5f * float(i);
        run(steer, slow, 16, si);
        expect(near(si[8] / plain(slow[8], steer), 1.25f, 1e-3f), "z rising at half the game's rate: x 1.25 (gain 0.5 x 0.5)");
        // Review P2: a main jet pushed into brake in one frame (z 1 -> 5) rises from its remembered z: the flash.
        float cz[16], ci[16];
        cz[0] = 1.f; for (unsigned i = 1; i < 16; ++i) cz[i] = 5.f;
        run(unsigned(ee::white) << ee::cluster_shift, cz, 16, ci);
        expect(near(ci[1] / ci[15], 1.5f) && ci[2] < ci[1] && near(ci[2] / ci[15], 1.f + .5f * (1.f - 1.f / 60.f / .12f)),
               "main jet z 1 -> brake z 5 in one frame: the brake body flares from the main jet's z");
        // Review P5: the rise over the time since the key was last seen. A puff at z 0.3 seen again after 0.4 s at z 0.8
        // (dz 0.5 over 400 game ms: 0.5 / 1.6 = 0.3125 of the full rise): x 1.156, not 1.5; past the 0.5 s hold the key is
        // new (no attack).
        {
            static Transients gap; gap.clear();
            float f0 = 0, f1 = 0, f2 = 0;
            gap.begin(1.f / 60.f); gap.attack(42, .3f, 1000.f / 60.f, &f0);
            for (unsigned i = 0; i < 24; ++i) gap.begin(1.f / 60.f); // 0.4 s unseen
            gap.attack(42, .8f, 1000.f / 60.f, &f1);
            static Transients gone; gone.clear();
            gone.begin(1.f / 60.f); gone.attack(43, .3f, 1000.f / 60.f, &f2);
            for (unsigned i = 0; i < 36; ++i) gone.begin(1.f / 60.f); // 0.6 s unseen
            gone.attack(43, .8f, 1000.f / 60.f, &f2);
            expect(f0 == 1.f && near(f1, 1.f + .5f * (.5f / (.004f * 400.f)), 2e-3f) && f2 == 1.f,
                   "a nozzle seen again after 0.4 s: the rise over 400 game ms (x 1.156, not 1.5); after 0.6 s a new key, no attack");
            std::printf("ATTACK_GAP factor_0.4s=%.4f factor_0.6s=%.4f\n", double(f1), double(f2));
        }
        // Capacity: 600 identities in one frame; eviction after the hold.
        static Transients mem; mem.clear(); mem.begin(1.f / 60.f);
        float factor = 0; unsigned stored = 0;
        for (std::uint64_t k = 1; k <= 600; ++k) { mem.attack(k, .5f, 16.7f, &factor); }
        for (const auto& s : mem.slots) stored += s.key != 0;
        expect(stored <= 512 && stored + mem.overflow == 600 && mem.overflow > 0, "600 identities: at most 512 slots, the rest counted overflow");
        mem.begin(.6f);
        stored = 0; for (const auto& s : mem.slots) stored += s.key != 0;
        expect(stored == 0, "a slot unseen for 0.5 s is free");
    }
    // ----------------------------------------------------------- gap 7: SETA and the travel look
    {
        bool on = true; float req = 0, rate = 0;
        expect(seta_decode(0x60000u, 0x10000u, &on, &req, &rate) && on && req == 6.f && rate == 6.f, "warp x6, governor 1: engaged, rate 6");
        expect(seta_decode(0xa0000u, 0x4cccu, &on, &req, &rate) && on && req == 10.f && near(rate, 3.f, 1e-3f), "x10 under the governor's floor 0.3: rate 3");
        expect(seta_decode(0x10000u, 0x10000u, &on, &req, &rate) && !on && rate == 1.f, "1.0: not engaged");
        const std::uint32_t bad[][2] = {{0, 0x10000u}, {0x640001u, 0x10000u}, {0x60000u, 0x4ccbu}, {0x60000u, 0x10001u}, {0xffffffffu, 0xffffffffu}};
        bool refused = true;
        for (const auto& b : bad) refused = refused && !seta_decode(b[0], b[1], &on, &req, &rate) && !on && req == 1.f && rate == 1.f;
        expect(refused && seta_decode(0x640000u, 0x4cccu, &on, &req, &rate) && on, "out of range: refused, 1.0 (fail closed); the bounds inclusive");
        expect(seta_slot_va == 0x00606f34u && seta_site_va == 0x004d1ef0u && seta_warp_offset == 0xccu && expected_seta_site[2] == 0xd0 &&
               expected_seta_site[8] == 0xcc && expected_seta_site[0] == 0x8b && expected_seta_site[6] == 0x8b, "the read: *0x00606f34 + 0xcc, bound by mov edx,[ecx+0xd0] / mov eax,[ecx+0xcc] at 0x004d1ef0");
        TravelRamp t; float w = 0; unsigned changes = 0;
        for (unsigned i = 0; i < 15; ++i) changes += t.step(true, 1.f / 60.f);
        t.weight(&w);
        expect(changes == 1 && t.engaged && near(t.linear, .5f, 1e-4f) && near(w, .5f, 1e-4f), "engaged at once, half way after 0.25 s");
        for (unsigned i = 0; i < 16; ++i) changes += t.step(true, 1.f / 60.f);
        t.weight(&w);
        expect(changes == 1 && w == 1.f, "full after 0.5 s (and held)");
        for (unsigned i = 0; i < 12; ++i) changes += t.step(false, 1.f / 60.f); // 0.2 s at 1.0
        for (unsigned i = 0; i < 3; ++i) changes += t.step(true, 1.f / 60.f);
        expect(changes == 1 && t.engaged, "a 0.2 s dip to 1.0 inside the hold: still engaged");
        changes += t.step(false, .1f);
        expect(changes == 1 && t.engaged && near(t.released, .1f), "0.1 s at 1.0 counts 0.1 s of the hold");
        for (unsigned i = 0; i < 11; ++i) changes += t.step(false, 1.f / 60.f);
        const bool held = t.engaged && changes == 1;
        for (unsigned i = 0; i < 2; ++i) changes += t.step(false, 1.f / 60.f);
        expect(held && changes == 2 && !t.engaged, "released once 0.3 s at 1.0 have passed (0.283 s: held): one change");
        for (unsigned i = 0; i < 40; ++i) t.step(false, 1.f / 60.f);
        t.weight(&w);
        expect(w == 0.f, "back to 0 after the ramp");
        TravelRamp u; const bool flipped = u.step(false, .1f); float wu = 1; u.weight(&wu);
        expect(!flipped && !u.engaged && wu == 0.f, "a 0.1 s stall frame at 1.0: no trigger");
        // Review P3: a gap without plume frames (the stage clock's one long step) with SETA off releases a full ramp at
        // once; with SETA on a stall is held to 0.1 s (no completed rise).
        TravelRamp g; for (unsigned i = 0; i < 40; ++i) g.step(true, 1.f / 60.f);
        const bool released = g.step(false, 2.f); float wg = 1; g.weight(&wg);
        TravelRamp h; h.step(true, 5.f);
        expect(released && !g.engaged && wg == 0.f && near(h.linear, .2f, 1e-5f) && h.engaged,
               "a 2 s gap at 1.0 releases a full ramp at once (weight 0); a 5 s stall under SETA rises 0.1 s (0.2)");
        // The look at weight 1 (warp 6 after the ramp): a main jet's L x 2 and radiance x 1.25; RCS unchanged.
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        Dynamics d0, d1; d1.travel = 1.f;
        build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat, nullptr, nullptr, &d0);
        const float L0 = out[0].local[2], I0 = out[0].intensity[0];
        build(&r, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat, nullptr, nullptr, &d1);
        const float L1 = out[0].local[2], I1 = out[0].intensity[0];
        const ee::Record puff = rec(0, 0, Z, -1, 0, 0, V, .5f, (unsigned(ee::white) << ee::cluster_shift) | ee::flag_steering);
        build(&puff, 1, nullptr, v, Preset::standard, 0, out.data(), 16, &st, nullptr, &flat, nullptr, nullptr, &d1);
        expect(near(L1 / L0, 2.f) && near(I1 / I0, 1.25f) && near(out[0].local[2], .5f * V) && near(out[0].intensity[0], (IL + (IH - IL) * (.25f / 1.75f)) * .5f),
               "travel weight 1: L x 2, I x 1.25 on a main jet; an RCS jet unchanged");
    }
    // ----------------------------------------------------------- the nozzle width setting
    {
        float w = 0;
        const char* accepted[] = {"0.25", "0.5", "1", "1.0", ".5", "0.1", "0.10", "0.123456"};
        const float values[] = {.25f, .5f, 1.f, 1.f, .5f, .1f, .1f, .123456f};
        for (unsigned i = 0; i < 8; ++i) { w = 0; expect(parse_nozzle(accepted[i], std::strlen(accepted[i]), &w) && near(w, values[i], 1e-6f), accepted[i]); }
        const char* refused[] = {"", "0.09", "1.01", "-0.5", "+0.5", "0.5 ", " 0.5", "5e-1", "0..5", ".", "abc", "0,5", "0.5000000000001", "2"};
        for (const char* t : refused) { w = 7.f; expect(!parse_nozzle(t, std::strlen(t), &w) && w == 7.f, t); }
        expect(parse_nozzle(L"0.5", 3, &w) && w == .5f, "wide text");
        expect(default_look.nozzle_width == .5f && nozzle_min == .1f && nozzle_max == 1.f, "default 0.5 (flight C), range 0.1..1.0");
        // The plume floor setting (after flight D): the same plain decimal, 0..3 (the scale of the k(R) curve).
        const char* floors_ok[] = {"0", "1", "3", ".25", "1.5"};
        const float floor_values[] = {0.f, 1.f, 3.f, .25f, 1.5f};
        for (unsigned i = 0; i < 5; ++i) { w = 7.f; expect(parse_floor(floors_ok[i], std::strlen(floors_ok[i]), &w) && near(w, floor_values[i], 1e-6f), floors_ok[i]); }
        const char* floors_bad[] = {"", "3.01", "-0.1", "4", "0.1 ", "1e-1", "nan"};
        for (const char* t : floors_bad) { w = 7.f; expect(!parse_floor(t, std::strlen(t), &w) && w == 7.f, t); }
        expect(parse_floor(L"0.2", 3, &w) && near(w, .2f, 1e-6f) && floor_min == 0.f && floor_max == 3.f, "floor: wide text, range 0..3");
        // The knob changes the proportions: n = 0.25 value, L unchanged.
        Look narrow = default_look; narrow.nozzle_width = .25f; narrow.pulse = 0.f;
        const ee::Record r = rec(0, 0, Z, -1, 0, 0, V, 2.f);
        build(&r, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st, nullptr, &narrow);
        expect(near(out[0].local[3], .25f * V) && near(out[0].local[2], 2.f * V), "nozzle 0.25: n = value / 4, L = z value");
    }
    // ----------------------------------------------------------- the pulse cache: one evaluation per seed byte
    {
        std::vector<ee::Record> rs;
        for (unsigned i = 0; i < 600; ++i) { ee::Record r = rec(float(i % 7) * 10.f, 0, Z, -1, 0, 0, V, 2.f); r.serial = i * 2654435761u; r.node_handle = i; rs.push_back(r); }
        std::vector<Vertex> vb(rs.size() * 8);
        const unsigned n = build(rs.data(), unsigned(rs.size()), nullptr, v, Preset::standard, 3.25f, vb.data(), unsigned(rs.size()), &st);
        bool same = n == rs.size();
        for (unsigned i = 0; i < n && same; ++i) {
            float want = 0;
            length_pulse(default_look, seed_byte(rs[i]), 3.25f, &want);
            same = vb[i * 8].local[2] == 2.f * V * want;
        }
        expect(same, "the cached pulse equals the direct evaluation for 600 records");
    }
    // ----------------------------------------------------------- the pixel program's look constants
    {
        float c[pixel_constant_floats];
        pixel_constants(default_look, c);
        expect(near(c[0], .575f) && near(c[1], (.04f - .575f) * .45f) && near(c[2], .575f * .45f) && near(c[3], .6f * .45f) && near(c[4], 12.5f) &&
               near(c[5], .57f * 1.6f) && near(c[6], 1.32f) && near(c[7], .4f) && near(c[8], .85f) && near(c[9], 6.2831853f / .16f) &&
               near(c[10], 3.f) && near(c[11], .84f) && near(c[12], HT) && near(c[13], 1.f / (.45f * .45f)) && near(c[14], .529f) &&
               c[16] == .3f && c[17] == .8f && c[19] == 2.f && default_look.bulge == 1.15f && default_look.tail_narrowing == .6f && default_look.ring == RG && c[52] == DIP && c[53] == RAMP,
               "look constants c3..c7 from the chosen settings (bulge 1.15, the mock-up's tail; ring 0.3 after flight C; c4.x 1 / period; "
               "the heat c6.x 0.7 (flight F's 0.1 reverted), the mouth dip / ramp c16.xy; "
               "the revised law: c4.x 2 / period (the cells' half-period ramp), erosion 1.6 erode, the cells' gap 0.85, the hot core 1 / (0.45 core))");
        expect(pixel_constant_floats == 68 && c[64] == 2.f && c[65] == 0.f && c[66] == 0.f && c[67] == 0.f && near(c[60], 3.74f) && near(c[61], 4.f * 1.25f) && c[62] == 1.f && near(c[63], 1.6f) && c[54] == .15f && near(c[55], .5f) && c[56] == .8f && c[57] == 1.f && near(c[58], 1.f / 300.f) &&
                   near(c[59], .35f) && default_look.spill_depth_max == 300.f,
               "the spill (gap 3): c16.zw glow_through 0.15, 1 / spill_depth 0.5; c17.xyz inner 0.8, reach 1.0 nozzle widths, 1 / 300 world units (the guard's bound); c17.w the tail's tongues 0.35; c18.xyz the core's widening 3.74 at detail 0, the outer sheath 4 x 1.25, the end-on ring's scale 1 / 1, the end-on annulus 1.6; c19.x 1 / disc_radius 2");
        // c8..c15: the law at u_k = (k + 0.5) / 8 against an independent replica (std::exp / std::cos).
        auto ss = [](float e0, float e1, float x) { float q = (x - e0) / (e1 - e0); q = q < 0 ? 0 : q > 1 ? 1 : q; return q * q * (3 - 2 * q); };
        float worst = 0.f;
        for (unsigned k = 0; k < disc_samples; ++k) {
            const float u = (float(k) + .5f) / 8.f;
            const float b = .575f * (1 - .55f * std::exp(-9 * u)) * (1 + .35f * ss(0, .25f, u) * std::exp(-4 * u));
            const float w = std::min(.575f + (.04f - .575f) * .45f * u, b + .575f * .45f) * std::max(1 - .6f * .45f * ss(.6f, 1, u), .05f);
            const float tl = (1 - ss(.4f, 1, u)) * std::exp(-.84f * u) * (1 - DIP * (1 - ss(0, RAMP, u)));
            const float cr = .5f + .5f * std::cos(6.2831853f * u / .16f);
            const float cl = .85f * std::exp(-3.f * u) * ss(0, .08f, u) * (1 - cr * cr * cr);
            const float ht = HT * (1 - ss(.05f, .3f, u));
            const float want[4] = {1.f / w, tl, cl, ht};
            for (unsigned j = 0; j < 4; ++j) worst = std::max(worst, std::fabs(c[20 + 4 * k + j] - want[j]));
        }
        expect(worst < 2e-5f, "c8..c15: 1 / w, tail, carve, heat at the disc's 8 samples");
        std::printf("DISC_TABLE max_abs_error=%.2e\n", double(worst));
        float ce = 0.f, ee_ = 0.f;
        for (unsigned i = 0; i < 4001; ++i) {
            const float x = -40.f + float(i) * .02f;
            float a = 0.f; law::cos(x, &a); ce = std::max(ce, std::fabs(a - float(std::cos(double(x)))));
            const float y = float(i) * .005f;
            float e = 0.f; law::exp_neg(y, &e); ee_ = std::max(ee_, std::fabs(e - float(std::exp(-double(y)))) / float(std::exp(-double(y))));
        }
        expect(ce < 2e-6f && ee_ < 2e-5f, "law::cos within 2e-6 on [-40, 40], law::exp_neg within 2e-5 relative on [0, 20]");
        std::printf("LAW cos_max_abs_error=%.2e exp_max_rel_error=%.2e\n", double(ce), double(ee_));
    }
    // ----------------------------------------------------------- the plume floor: k x the ship's radius, at most 4 x value
    {
        const float Zf = 9000.f;
        // The anchors' arithmetic at scale 1 (the curve as first chosen); the shipped scale is 0.5 since flight E.
        Look curve = default_look;
        curve.floor_scale = 1.f;
        Look flat1 = flat;
        flat1.floor_scale = 1.f;
        const unsigned steer = (unsigned(ee::white) << ee::cluster_shift) | ee::flag_steering;
        const unsigned brake = (unsigned(ee::white) << ee::cluster_shift) | ee::flag_brake;
        // One ship of radius 5,000 (record units; k(5,000) 0.1: the floor 500): its 1,000 main jet is culled this frame (absent),
        // a 200 secondary rises to 500, a 1,000 stays, a 100 rises to its 4 x cap 400, an RCS 300 and a brake-pushed 100
        // keep theirs; another ship (radius 0: unknown) keeps its 200 and counts floor_unknown.
        const ee::Record rs[6] = {rec(0, 1000, Zf, -1, 0, 0, 200.f, 2.f), rec(-4000, 0, Zf, -1, 0, 0, 1000.f, 2.f),
                                  rec(0, -1000, Zf, -1, 0, 0, 100.f, 2.f), rec(2000, 0, Zf, -1, 0, 0, 300.f, .5f, steer),
                                  rec(2000, 1500, Zf, -1, 0, 0, 100.f, 6.f, brake), rec(-2000, -1500, Zf, -1, 0, 0, 200.f, 2.f)};
        const float radii[6] = {5000.f, 5000.f, 5000.f, 5000.f, 5000.f, 0.f};
        std::vector<Vertex> vb(6 * 8);
        expect(default_look.floor_scale == .5f && default_look.floor_r[0] == 150.f && default_look.floor_r[1] == 500.f &&
                   default_look.floor_r[2] == 5000.f && default_look.floor_k[0] == .35f && default_look.floor_k[1] == .25f &&
                   default_look.floor_k[2] == .1f && default_look.floor_cap == 4.f,
               "the plume floor's defaults: k 0.35 at R <= 150, 0.25 at 500, 0.10 at >= 5,000, scale 0.5 (after flight E), cap 4 x value");
        // k(R): log-linear between the anchors, against an independent replica (std::log); ln without x87.
        auto k_of = [](float R) {
            if (R <= 150.f) return .35f;
            if (R >= 5000.f) return .1f;
            if (R < 500.f) return .35f + (.25f - .35f) * float(std::log(double(R) / 150.) / std::log(500. / 150.));
            return .25f + (.1f - .25f) * float(std::log(double(R) / 500.) / std::log(10.));
        };
        float kmax = 0.f, lnmax = 0.f;
        for (float R : {1.f, 67.3f, 150.f, 159.7f, 273.86f, 467.f, 500.f, 501.f, 800.f, 1581.14f, 3000.f, 4999.f, 5000.f, 10022.f, 1e7f}) {
            float k = 0.f; floor_ratio_at(curve, R, &k);
            kmax = std::max(kmax, std::fabs(k - k_of(R)));
        }
        for (float x : {1e-6f, .01f, .5f, 1.f, 1.5f, 2.f, 3.14159f, 10.f, 1234.5f, 3e30f}) {
            float l = 0.f; law::ln(x, &l);
            lnmax = std::max(lnmax, std::fabs(l - float(std::log(double(x)))) / std::max(1.f, std::fabs(float(std::log(double(x))))));
        }
        float k0 = 1.f, kn = 1.f, kmid = 0.f, kmid2 = 0.f, k2 = 0.f;
        Look none = default_look; none.floor_scale = 0.f;
        Look twice = default_look; twice.floor_scale = 2.f;
        floor_ratio_at(none, 100.f, &k0);
        floor_ratio_at(default_look, NAN, &kn);
        floor_ratio_at(curve, 1581.1388f, &kmid);
        floor_ratio_at(curve, 273.86128f, &kmid2);
        floor_ratio_at(twice, 10022.f, &k2);
        expect(kmax < 1e-5f && lnmax < 2e-6f && k0 == 0.f && kn == 0.f && near(kmid, .175f, 1e-5f) && near(kmid2, .30f, 1e-5f) && near(k2, .2f),
               "k(R): 0.35 / 0.25 / 0.10 at 150 / 500 / 5,000, the geometric means halfway; scale 2 doubles, 0 off; law::ln within 2e-6");
        std::printf("FLOOR_K max_abs_error=%.2e ln_max_rel_error=%.2e\n", double(kmax), double(lnmax));
        // value_eff (the census): run406's ships (record units).
        struct Ref { float R, v, want; };
        const Ref refs[] = {{467.f, 40.f, 467.f * k_of(467.f)}, {10022.f, 939.22f, 1002.2f}, {10022.f, 187.5f, 750.f}, {67.3f, 10.f, 23.555f},
                            {67.3f, 5.04f, 20.16f}, {159.7f, 10.f, 40.f}, {159.7f, 5.04f, 20.16f}};
        bool refs_ok = true;
        for (const Ref& x : refs) {
            ee::Record q = rec(0, 0, Zf, -1, 0, 0, x.v, 2.f);
            float ve = 0.f; floored_value(curve, q, x.R, &ve);
            refs_ok = refs_ok && near(ve, x.want, 1e-4f);
            q.flags |= ee::flag_steering; floored_value(curve, q, x.R, &ve);
            refs_ok = refs_ok && ve == x.v;
        }
        std::printf("FLOOR_REFS m6_k=%.4f m6_value_eff=%.2f\n", double(k_of(467.f)), double(467.f * k_of(467.f)));
        expect(refs_ok, "value_eff on run406's ships: M6 40 -> 119.4, capital 939.2 -> 1,002.2 and 187.5 -> 750 (cap), M4 10 -> 23.6 / 5 -> 20.2 (cap), TS 10 -> 40 / 5 -> 20.2 (caps); RCS kept");
        const unsigned n = build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &st, nullptr, &flat1, nullptr, radii);
        bool ok = n == 6 && st.floored == 2 && st.floor_unknown == 1;
        ok = ok && near(vb[0].shape[1], 500.f) && near(vb[0].local[3], .5f * 500.f) && near(vb[0].local[2], 2.f * 500.f) &&
             near(vb[8].shape[1], 1000.f) && near(vb[16].shape[1], 400.f) && near(vb[24].shape[1], 300.f) &&
             near(vb[32].shape[1], 100.f) && near(vb[40].shape[1], 200.f);
        // The nozzle's position is the record's: the floored plume's disc sits at its own origin.
        float cxs = 0.f, cys = 0.f;
        for (unsigned c = 4; c < 8; ++c) { cxs += vb[c].position[0] * .25f; cys += vb[c].position[1] * .25f; }
        ok = ok && std::fabs(cxs) < 1e-2f && std::fabs(cys - 1000.f) < 1e-2f;
        expect(ok, "floor: the 200 secondary of a ship whose main jet is culled draws at k x radius (500), a 100 at its 4 x cap, the 1,000 kept, RCS and brake untouched, radius 0 no floor");
        // Without radii, or with the floor off (k 0): nothing raised, nothing counted.
        build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &st, nullptr, &flat);
        expect(st.floored == 0 && st.floor_unknown == 0 && near(vb[0].shape[1], 200.f), "no radii: no floor");
        Look off = flat;
        off.floor_scale = 0.f;
        build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &st, nullptr, &off, nullptr, radii);
        expect(st.floored == 0 && st.floor_unknown == 0 && near(vb[0].shape[1], 200.f), "floor_scale 0: the floor off");
        // A non-finite or negative radius takes no floor.
        const float bad[6] = {NAN, -5000.f, INFINITY, 0.f, 0.f, 0.f};
        build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &st, nullptr, &flat1, nullptr, bad);
        expect(st.floored == 0 && st.floor_unknown == 4 && near(vb[0].shape[1], 200.f) && near(vb[16].shape[1], 100.f), "NaN, negative, infinite radius: no floor");
        // A positive garbage root radius: above 2,000 x the record's value it is unknown (0, no floor, floor_unknown),
        // not the 4x cap; just under the bound it stands.
        {
            float garbage = 1.f, plausible = 0.f;
            ee::parent_radius_in_record(2000001, 200u, 0x10000u, 200.f, &garbage);  // 10,000.005 x the value
            ee::parent_radius_in_record(1999999, 200u, 0x10000u, 200.f, &plausible); // 9,999.995 x
            float with_garbage[6];
            for (unsigned i = 0; i < 6; ++i) with_garbage[i] = radii[i];
            with_garbage[0] = garbage;
            BuildStats base{};
            build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &base, nullptr, &flat1, nullptr, radii);
            build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &st, nullptr, &flat1, nullptr, with_garbage);
            expect(garbage == 0.f && near(plausible, 1999999.f) && ee::parent_radius_max_ratio == 10000.f &&
                       st.floor_unknown == base.floor_unknown + 1 && st.floored + 1 == base.floored && near(vb[0].shape[1], 200.f),
                   "a root radius above 10,000 x the value: unknown (no floor, floor_unknown), not the 4x cap");
        }
        // The scene-view filter: a hidden record is skipped before the floor (nothing counted for it).
        const std::uint32_t cam[6] = {1, 1, 1, 1, 1, 1};
        const std::uint8_t scene[6] = {0, 1, 1, 1, 1, 1};
        ViewFilter vf; vf.camera = cam; vf.scene = scene; vf.handle = 1;
        build(rs, 6, nullptr, v, Preset::standard, 0.f, vb.data(), 6, &st, &vf, &flat1, nullptr, radii);
        expect(st.floored == 1 && st.skipped_other_view == 1 && st.floor_unknown == 1 && near(vb[0].shape[1], 1000.f), "the filter: a hidden record takes no floor");
        // 1,024 records: each value min(max(value, 0.1 radius), 3 value).
        std::vector<ee::Record> many;
        std::vector<float> rad;
        for (unsigned i = 0; i < 1024; ++i) {
            many.push_back(rec(float(i % 32) * 50.f - 800.f, float(i / 32) * 50.f - 800.f, Zf, -1, 0, 0, 100.f + float(i % 7) * 100.f, 2.f));
            rad.push_back(float(i % 11) * 1000.f);
        }
        std::vector<Vertex> big(1024 * 8);
        const unsigned drawn = build(many.data(), 1024, nullptr, v, Preset::standard, 0.f, big.data(), 1024, &st, nullptr, &flat1, nullptr, rad.data());
        bool all = drawn == 1024;
        for (unsigned i = 0; i < 1024 && all; ++i)
            all = near(big[i * 8].shape[1], rad[i] > 0.f ? std::max(many[i].size, std::min(k_of(rad[i]) * rad[i], 4.f * many[i].size)) : many[i].size, 1e-4f);
        expect(all, "1,024 records: each value min(max(value, k(R) x R), 4 x value)");
        float half = 0.f;
        ee::Record h = rec(0, 0, Zf, -1, 0, 0, 200.f, 2.f);
        floored_value(default_look, h, 5000.f, &half);
        expect(near(half, 250.f), "the shipped scale 0.5: a 200 secondary of a radius-5,000 ship draws at 0.5 x 0.1 x 5,000 = 250");
    }
    // ----------------------------------------------------------- the ship radius in the record's units
    {
        float r = -1.f;
        // run406's capital: size 939.211 for value 93922 (+0x70) at x scale 1 (+0x80 0x10000): context 0.01.
        ee::parent_radius_in_record(1002246, 93922u, 0x10000u, 939.211f, &r);
        const bool scaled = near(r, 1002246.f * 939.211f / 93922.f, 1e-5f);
        float z1 = 1.f, z2 = 1.f, z3 = 1.f, z4 = 1.f, z5 = 1.f;
        ee::parent_radius_in_record(-1, 93922u, 0x10000u, 939.211f, &z1);      // dirty (-1)
        ee::parent_radius_in_record(0, 93922u, 0x10000u, 939.211f, &z2);       // unread
        ee::parent_radius_in_record(1000, 0u, 0x10000u, 939.211f, &z3);        // no scale
        ee::parent_radius_in_record(1000, 93922u, 0x10000u, NAN, &z4);         // no size
        ee::parent_radius_in_record(1000, 0x80000000u, 0x10000u, 939.211f, &z5); // a negative +0x70
        expect(scaled && z1 == 0.f && z2 == 0.f && z3 == 0.f && z4 == 0.f && z5 == 0.f,
               "the ship radius in record units: radius x size / (+0x70 x +0x80 / 65536); 0 when dirty, unread or unscaled");
    }
    // ----------------------------------------------------------- the disc's distance law (after Run 127)
    {
        // 0.5 + 0.5 smoothstep(20, 160, px): 0.5 at <= 20 px, 0.5 + 0.5 x 0.2435 at 65 (t 0.3214), 1 from 160 px. An
        // end-on nozzle's disc terms (body, halo, cap, ring) scale by it on the drawn disc width, the axial quad does not;
        // at 6 px the far law multiplies on top.
        expect(default_look.disc_far_low == .5f && default_look.disc_px_min == 20.f && default_look.disc_px_full == 160.f,
               "disc distance law constants 0.5 / 20 / 160 px");
        Look law_on = flat, law_off = flat;
        law_on.disc_far_low = default_look.disc_far_low;
        const float at[5] = {6.f, 20.f, 65.f, 160.f, 300.f};
        bool ok = true;
        for (unsigned i = 0; i < 5; ++i) {
            float w = 0.f, t = 0.f;
            disc_distance_weight(default_look, at[i], &w);
            law::smooth(20.f, 160.f, at[i], &t);
            ok = ok && near(w, .5f + .5f * t);
            const ee::Record r = rec(0, 0, 6.f * Z, 0, 0, -1, 2.f * at[i] / ppu(6.f * Z), 2.f);
            std::vector<Vertex> a(8), b(8);
            BuildStats sa{}, sb{};
            build(&r, 1, nullptr, v, Preset::standard, 0.f, a.data(), 1, &sa, nullptr, &law_on);
            build(&r, 1, nullptr, v, Preset::standard, 0.f, b.data(), 1, &sb, nullptr, &law_off);
            ok = ok && sa.discs == 1 && near(.5f * b[4].shape[1] * ppu(6.f * Z), at[i], 1e-2f); // the natural width (near cap or not)
            for (unsigned j = 0; j < 3; ++j) ok = ok && near(a[4].intensity[j], b[4].intensity[j] * w, 1e-4f) && a[0].intensity[j] == b[0].intensity[j];
            ok = ok && near(a[4].local[2], b[4].local[2] * w, 1e-4f);
            // The own ship's jet (Ring::own) is exempt: its disc equals the law-off draw's.
            const std::uint8_t own_tag[1] = {1};
            std::vector<Vertex> c(8);
            BuildStats sc{};
            build(&r, 1, nullptr, v, Preset::standard, 0.f, c.data(), 1, &sc, nullptr, &law_on, nullptr, nullptr, nullptr, nullptr, own_tag);
            for (unsigned j = 0; j < 3; ++j) ok = ok && c[4].intensity[j] == b[4].intensity[j] && c[0].intensity[j] == b[0].intensity[j];
            ok = ok && c[4].local[2] == b[4].local[2];
        }
        float w65 = 0.f;
        disc_distance_weight(default_look, 65.f, &w65);
        expect(ok && near(w65, .5f + .5f * .2435f, 2e-3f),
               "disc distance law: the disc's body, halo, cap and ring x 0.5 + 0.5 smoothstep(20, 160, px) at 6 / 20 / 65 / 160 / 300 px, the axial quad unchanged; the own ship's jet exempt");
    }
    // ----------------------------------------------------------- the disc's L / n bound and the cached tables
    {
        // Nozzle 0.1: L / n = 20 at full throttle, held to 8 in the disc's body and halo gains (head-on, flat look).
        Look thin = flat;
        thin.nozzle_width = .1f;
        const float V = 40.f / ppu(Z);
        const ee::Record at = rec(0, 0, Z, 0, 0, -1, V, 2.f);
        build(&at, 1, nullptr, v, Preset::standard, 0.f, out.data(), 16, &st, nullptr, &thin);
        expect(near(out[4].intensity[0], IH * 1.84f * 8.f * fw4) && (out[4].tint >> 24) == 0u && near(out[4].intensity[1], HB * 3.f * 8.f * fw4) &&
                   near(out[0].local[2] / out[0].local[3], 20.f, 1e-3f),
               "nozzle 0.1: the disc's L / n held to 8 (the quad keeps L / n 20)");
        // The cached tables give the same vertices as tables computed per build.
        LookTables tb;
        look_tables(flat, &tb);
        std::vector<Vertex> a(8), b(8);
        build(&at, 1, nullptr, v, Preset::standard, 2.f, a.data(), 1, &st, nullptr, &flat);
        build(&at, 1, nullptr, v, Preset::standard, 2.f, b.data(), 1, &st, nullptr, &flat, &tb);
        float c1[pixel_constant_floats], c2[pixel_constant_floats];
        pixel_constants(flat, c1);
        pixel_constants(flat, tb, c2);
        expect(!std::memcmp(a.data(), b.data(), 8 * sizeof(Vertex)) && !std::memcmp(c1, c2, sizeof c1),
               "cached look tables: the build and the pixel constants unchanged");
    }
    // ----------------------------------------------------------- cost of the build
    // With the look's tables cached as the proxy does (MotionOutput::plumes_tables_), plain and with the plume floor
    // (every record a radius: the floor's compare and multiply per main jet).
    LookTables cached;
    look_tables(default_look, &cached);
    for (const unsigned count : {30u, 100u, 1024u}) {
        std::vector<ee::Record> rs;
        std::uint32_t seed = 99;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.f; };
        for (unsigned i = 0; i < count; ++i) { const float z = 1500.f + 28500.f * rnd(); rs.push_back(rec((rnd() - .5f) * z, (rnd() - .5f) * z * .5f, z, rnd() - .5f, rnd() - .5f, rnd() - .5f, (3.f + 57.f * rnd()) / ppu(z), .25f + 1.75f * rnd())); }
        std::vector<float> radii(count);
        for (unsigned i = 0; i < count; ++i) radii[i] = rs[i].size * (5.f + 10.f * rnd());
        std::vector<Vertex> vb(std::size_t(count) * 8);
        // The proxy's path (review P1): the floor and the per-nozzle memory (every record its own identity; past 512
        // drawn nozzles the window overflows), begun each frame.
        std::vector<ee::Record> rm = rs;
        for (unsigned i = 0; i < count; ++i) rm[i].node_handle = 100u + i;
        static Transients memory; memory.clear();
        std::vector<double> us, fl, mm;
        unsigned drawn = 0, drawn_floor = 0, drawn_memory = 0;
        for (unsigned rep = 0; rep < 31; ++rep) {
            auto a = std::chrono::steady_clock::now();
            for (unsigned k = 0; k < 20; ++k) drawn = build(rs.data(), count, nullptr, v, Preset::standard, float(rep * 20 + k) / 60.f, vb.data(), count, nullptr, nullptr, nullptr, &cached);
            us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count() / 20.);
            a = std::chrono::steady_clock::now();
            for (unsigned k = 0; k < 20; ++k) drawn_floor = build(rs.data(), count, nullptr, v, Preset::standard, float(rep * 20 + k) / 60.f, vb.data(), count, nullptr, nullptr, nullptr, &cached, radii.data());
            fl.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count() / 20.);
            a = std::chrono::steady_clock::now();
            for (unsigned k = 0; k < 20; ++k) {
                Dynamics dy; dy.transients = &memory; dy.step = 1.f / 60.f; dy.game_ms = 1000.f / 60.f; dy.flow = double(rep * 20 + k) * .04375;
                drawn_memory = build(rm.data(), count, nullptr, v, Preset::standard, float(rep * 20 + k) / 60.f, vb.data(), count, nullptr, nullptr, nullptr, &cached, radii.data(), &dy);
            }
            mm.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count() / 20.);
        }
        std::sort(us.begin(), us.end());
        std::sort(fl.begin(), fl.end());
        std::sort(mm.begin(), mm.end());
        std::printf("BUILD records=%u drawn=%u median_us=%.2f\n", count, drawn, us[us.size() / 2]);
        std::printf("BUILD_FLOOR records=%u drawn=%u median_us=%.2f\n", count, drawn_floor, fl[fl.size() / 2]);
        std::printf("BUILD_MEMORY records=%u drawn=%u median_us=%.2f overflow=%u\n", count, drawn_memory, mm[mm.size() / 2], memory.overflow);
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


class NozzleOption(unittest.TestCase):
    """engine_plume_nozzle (2026-10-03): the plume's nozzle width in value, load-time; flight C chose 0.5, the default."""

    def test_schema_entry(self):
        e = schema.BY_KEY['engine_plume_nozzle']
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['launcher'], e['developer']),
                         ('X3M_ENGINE_PLUME_NOZZLE', 'float', 'engine', None, '0.5', '--engine-plume-nozzle', False))
        self.assertEqual(e['range'], ((0.1, 1.0, False),))
        self.assertIn('{"X3M_ENGINE_PLUME_NOZZLE", "engine_plume_nozzle", Type::Float, nullptr,',
                      (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn(';engine_plume_nozzle = 0.5', (ROOT / 'assets/x3m.ini').read_text())

    def test_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_PLUME_NOZZLE', launch_env(module, game, wine))
            self.assertNotIn('X3M_ENGINE_PLUME_NOZZLE', launch_env(module, game, wine, '--engine-effects', 'plumes'))
            for value, sent in (('0.5', '0.5'), ('0.25', '0.25'), ('1', '1'), ('0.1', '0.1')):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-plume-nozzle', value)
                self.assertEqual(env['X3M_ENGINE_PLUME_NOZZLE'], sent)
            for bad in ('0.05', '1.5', 'nan', 'inf'):
                with self.assertRaises(SystemExit):
                    launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-plume-nozzle', bad)
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-plume-nozzle', '0.5')

    def test_read_once_at_load(self):
        module = (ROOT / 'src/proxy/engine_effects.cpp').read_text()
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_PLUME_NOZZLE"', module)), 1)
        self.assertIn('x3m::engine_plumes::parse_nozzle(width, wn, &nozzle)', module)
        self.assertIn('nozzle=%.3f nozzle_setting=%s nozzle_status=%s', module)


class FloorOption(unittest.TestCase):
    """engine_plume_floor (2026-10-03, after flight D): the scale of the plume floor's k(R) curve, load-time; default 0.5
    since flight E (it was 1: the anchors 0.35 / 0.25 / 0.10 at R 150 / 500 / 5,000), 0 = the floor off."""

    def test_schema_entry(self):
        e = schema.BY_KEY['engine_plume_floor']
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['launcher'], e['developer']),
                         ('X3M_ENGINE_PLUME_FLOOR', 'float', 'engine', None, '0.5', '--engine-plume-floor', False))
        self.assertEqual(e['range'], ((0.0, 3.0, False),))
        self.assertIn('{"X3M_ENGINE_PLUME_FLOOR", "engine_plume_floor", Type::Float, nullptr,',
                      (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn(';engine_plume_floor = 0.5\n', (ROOT / 'assets/x3m.ini').read_text())

    def test_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_PLUME_FLOOR', launch_env(module, game, wine))
            self.assertNotIn('X3M_ENGINE_PLUME_FLOOR', launch_env(module, game, wine, '--engine-effects', 'plumes'))
            for value, sent in (('1', '1'), ('0', '0'), ('0.25', '0.25'), ('1.5', '1.5'), ('3', '3')):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-plume-floor', value)
                self.assertEqual(env['X3M_ENGINE_PLUME_FLOOR'], sent)
            for bad in ('-0.1', '3.1', 'nan', 'inf'):
                with self.assertRaises(SystemExit):
                    launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-plume-floor', bad)
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-plume-floor', '0.1')

    def test_read_once_at_load(self):
        module = (ROOT / 'src/proxy/engine_effects.cpp').read_text()
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_PLUME_FLOOR"', module)), 1)
        self.assertIn('x3m::engine_plumes::parse_floor(share, fn, &floor_ratio)', module)
        self.assertIn('floor=%.3f floor_setting=%s floor_status=%s', module)


class Wiring(unittest.TestCase):
    def test_wiring(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('configure_engine_plumes(engine_effects::mode()==engine_effects::core::Mode::plumes&&engine_effects::suppress(),'
                      'engine_effects::preset(),engine_effects::plume_nozzle(),engine_effects::plume_floor());', capture)
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        self.assertIn('if(plumes_requested_&&engine_plumes_arm(hdr_scene!=nullptr,depth,in.width,in.height)&&'
                      '(engine_ring_->count||engine_far_jets::count()||engine_ribbons_live())){in.stage_callback=&MotionOutput::engine_plumes_callback;', motion)
        self.assertIn('plumes_failures_=0;plumes_failed_out_=false;plumes_lane_=nullptr;', motion)  # before_reset
        self.assertIn('if(plumes_)taa_call([&]{plumes_->before_reset();});', motion)
        self.assertIn('if(plumes_)plumes_->after_reset(result);', motion)
        self.assertIn('release_engine_plumes();', motion)
        self.assertIn('#include "motion_output_engine_plumes_inc.h"', motion)
        inc = source_text(ROOT / 'src/proxy/motion_output_engine_plumes_inc.h')
        self.assertIn('plumes_disarmed_until_=frame_+plumes_disarm_frames;', inc)
        self.assertIn('static constexpr std::uint64_t plumes_disarm_frames=64;', source_text(ROOT / 'src/proxy/motion_output.h'))
        self.assertIn('glow=%s drawn=%s', inc)
        # Review fixes after flight C (2026-10-03): a stage that is not attached (refused, failed until Reset or
        # disarmed) forwards the glow natively from the next frame on; the look's tables cached at load.
        self.assertIn('return plumes_requested_&&(plumes_attach_failed_||plumes_failed_out_||frame_<plumes_disarmed_until_);', inc)
        self.assertIn('engine_plumes::look_tables(plumes_look_,&plumes_tables_);', inc)
        self.assertIn('in.tables=&plumes_tables_;', inc)
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
        # Review fixes (2026-10-03): the flow phase advances by the clock's step at a constant rate; the look carries the
        # configured nozzle width.
        # After the gap analysis (phases 2 and 3): the SETA read and the travel ramp before the flow, which advances x the
        # travel look's flow; the accumulator, the travel weight, the steps and the attack memory handed to the pass.
        self.assertIn('engine_seta_step(engine_clock_.last_step);const float travel_flow=1.f+(engine_plumes::travel_flow-1.f)*'
                      'engine_travel_weight_;engine_flow_.advance(engine_clock_.last_step,plumes_flow_rate_*travel_flow);'
                      'in.flow=engine_flow_.nozzle_widths;in.travel=engine_travel_weight_;in.step=float(engine_clock_.last_step);'
                      'in.game_ms=float(engine_clock_.last_step*1000.)*seta_rate_;in.transients=engine_transients_.get();'
                      'in.look=&plumes_look_;', inc)
        self.assertLess(inc.index('engine_clock_.wrapped(&in.seconds);'), inc.index('engine_flow_.advance('))
        self.assertIn('engine_plumes::flow_rate(plumes_look_,&plumes_flow_rate_);', inc)
        self.assertIn('const engine_effects::SetaStatus status=engine_effects::seta_read(&warp,&mult);++seta_reads_;', inc)
        self.assertIn('}else if(!engine_plumes::seta_decode(warp,mult,&engaged,&seta_warp_,&seta_rate_)){++seta_invalid_;', inc)
        self.assertIn('log("engine_seta device=%llu frame=%llu event=%s state=%s read=%s', inc)
        self.assertIn('if(!log_tier::cached_debug||(!changed&&!status_changed))return;', inc)
        # Review P3 / P7: the ramp starts over on a load epoch change and at Reset (one reset_load / reset_device row when
        # it was not at rest); valid is 0 for a refused read too.
        self.assertIn('if(engine_load_epoch_!=engine_travel_epoch_){if(engine_travel_.engaged||engine_travel_.linear>0.f)'
                      'engine_travel_reset_="reset_load";engine_travel_=engine_plumes::TravelRamp{};'
                      'engine_travel_epoch_=engine_load_epoch_;}', inc)
        self.assertIn('if(engine_travel_.engaged||engine_travel_.linear>0.f)engine_travel_reset_="reset_device";'
                      'engine_travel_=engine_plumes::TravelRamp{};engine_travel_weight_=0.f;', motion)
        self.assertIn('seta_valid_=status==engine_effects::SetaStatus::ok;', inc)
        self.assertIn('flow_factor_changes=%u', inc)
        self.assertIn('if(plumes_requested_&&!engine_transients_)engine_transients_.reset(new(std::nothrow)engine_plumes::Transients);', inc)
        effects_module = source_text(ROOT / 'src/proxy/engine_effects.cpp')
        self.assertIn('if(!identity_)return SetaStatus::identity;', effects_module)
        # Review P6: a failed read of the site refuses that frame (status read) and is retried; only a byte mismatch
        # latches the refusal (the effects fixture's armed mode executes both).
        self.assertIn('if(x3m::engine_memory::read(seta_site_,bytes,sizeof bytes))'
                      'seta_site_state_=!std::memcmp(bytes,ep::expected_seta_site,sizeof bytes)?1:2;', effects_module)
        self.assertIn('if(!seta_site_state_)status=SetaStatus::read;else if(seta_site_state_!=1)status=SetaStatus::site;', effects_module)
        self.assertIn('else if(!x3m::engine_memory::read(seta_slot_,&cfg,sizeof cfg)||!cfg)status=SetaStatus::pointer;'
                      'else if(!x3m::engine_memory::read(std::uintptr_t(cfg)+ep::seta_warp_offset,pair,sizeof pair))'
                      'status=SetaStatus::read;', effects_module)
        self.assertIn('SetLastError(error);return status;', effects_module)
        passes = source_text(ROOT / 'src/renderer/engine_plumes_pass.cpp')
        self.assertIn('float pixel[12+engine_plumes::pixel_constant_floats]={1.f/float(f.width),1.f/float(f.height),0.f,0.f,', passes)
        self.assertIn('{0,72,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,3},', passes)
        ribbon_pass = source_text(ROOT / 'src/renderer/engine_ribbons_pass.cpp')
        self.assertIn('f.filter.camera&&f.filter.scene?&f.filter:nullptr,f.look,f.radii,f.travel,f.parents);', ribbon_pass)
        ps = (ROOT / 'src/effects/engine_plume_ps.hlsl').read_text()
        self.assertIn(': float3(q.x - phase, q.y * 4.5, seed);', ps)
        self.assertIn('const float S2 = fbm(p * 2.2 + float3(5.0, 2.0, 1.0)) - 0.4375;', ps)
        self.assertEqual(ps.count(' fbm(p'), 1)  # one evaluation (the revised law's streak field)
        self.assertIn('const float phase = i.shape.w;', ps)
        self.assertIn('const bool disc = i.peak.w > 0.5;', ps)
        self.assertIn('const float rim = smoothstep(0.25, 0.9, radial);\n        const float3 colour = lerp(lerp(tone, 0.5 * tint, detail * rim), white, heat);', ps)
        # The tuning pass: the side view's outer sheath on the colour's rim window, the end-on ring's width.
        self.assertIn('const float rr = (rho - core_k.z) * lerp(1.0, detail_k.z, detail);', ps)
        # The single law (critique section 6, "One law"): the core's radius by the detail level, the structure x d, the
        # outer sheath, the tongues' max(S2, 0); no second law, no gain.
        self.assertIn('const float kc = lerp(detail_k.x, 1.0, detail);', ps)
        self.assertIn('const float3 head = lerp(tint, i.peak.rgb, detail);', ps)
        self.assertIn('const float core = edge * cells * (0.08 + 0.92 * exp(-rp * rp)) * (1.0 + 0.6 * hot * (1.0 - 0.5 * smoothstep(0.2, 0.8, uc)));', ps)
        self.assertIn('const float outer = detail * detail_k.y * m * (1.0 - m) * (1.0 - smoothstep(0.65, 1.2, radial + erosion)) * cells *', ps)
        self.assertIn('result = ((colour * core + 0.5 * darken * deep * outer) * body + soft_max(ring, ring_colour, halo, tint).rgb) * handover;', ps)
        self.assertIn('u + spill_k.w * max(S2, 0.0) * detail', ps)
        for gone in ('lerp(slab', 'const float slab', 'gain_k', 'const float peaked', '0.1953'):
            self.assertNotIn(gone, ps)
        self.assertIn('const float heat = core_k.x * hot * (1.0 - smoothstep(0.05, 0.3, u));', ps)
        self.assertIn('exp(-(2.2 + 1.8 * detail) * uc)', ps)
        self.assertIn('const float soft_halo = occluder ? valid * max(saturate(gap / max(look.y * i.shape.y, 1e-4)), spill) : 1.0;', ps)
        self.assertIn('const float soft_body = occluder ? valid * saturate(gap / max(look.x * i.shape.y, 1e-4)) : 1.0;', ps)
        self.assertIn('saturate(2.0 * (look.z - dn))', ps)
        # After flight C (2026-10-03): the end-on disc's samples in c8..c15, the soft-maximum mouth, the cells ramped in,
        # the ship key read beside the own-ship tag.
        self.assertIn('float4 disc_k[8] : register(c8);', ps)
        self.assertIn('const float handover = 1.0 - i.params.w * (1.0 - smoothstep(halo_k.x, halo_k.y, d_screen * handover_scale));', ps)
        # since Run 125: the disc's width; since Run 128 x 1 / disc_radius (c19.x)
        self.assertIn('const float handover_scale = min(i.peak.w * (255.0 / 127.0), 1.0) * radius_k.x;', ps)
        self.assertIn('float4 radius_k : register(c19);', ps)
        self.assertIn('smoothstep(0.0, 1.0, u * fire_k.x)', ps)
        self.assertIn('const float shown = cap * (1.0 - exp(-total / cap));', ps)
        effects_inc = source_text(ROOT / 'src/proxy/motion_output_engine_effects_inc.h')
        self.assertIn('parent_known=facts.scoped&&(scope.valid&object_trace::Node);scope_parent=parent_known?scope.parent:0u;', effects_inc)
        self.assertIn('engine_ring_->parent[slot]=parent_known?scope_parent:0u;', effects_inc)
        self.assertNotIn('engine_memory::read(scope.node+0x18', effects_inc)
        self.assertIn('engine_stage_off_=engine_plumes_stage_off();', effects_inc)
        self.assertIn('facts.stage_off=engine_stage_off_;', effects_inc)
        # After flight D (2026-10-03): the plume floor from the ship's radius, the parent's +0xa4 read once per parent
        # among the frame's four most recent (LastError preserved), only with the floor on or for a census row, in the
        # record's units beside the records, handed to the stage; the floor knob.
        self.assertIn('ee::parent_radius_in_record(parent_known?engine_parent_radius(scope_parent):0,scope.scale[0],scope.scale[1],'
                      'record.size,&jet_radius);', effects_inc)
        self.assertIn('engine_ring_->parent_radius[slot]=jet_radius;', effects_inc)
        self.assertIn('const bool known=engine_memory::read(std::uintptr_t(parent)+engine_effects::core::parent_radius_offset,&radius,'
                      'sizeof radius);SetLastError(error);', effects_inc)
        self.assertIn('if(engine_radius_parent_[i]==parent)return engine_radius_[i];', effects_inc)
        self.assertIn('engine_radius_next_=(slot+1)&(engine_radius_slots-1);', effects_inc)
        self.assertIn('if(jet&&((verdict==ee::Verdict::suppressed&&plumes_requested_&&plumes_look_.floor_scale>0.f)||census_row))',
                      effects_inc)
        self.assertIn('const bool census_row=engine_census_&&(capture_||engine_row_frames_<engine_row_frame_cap)&&'
                      'engine_rows_<engine_row_cap;', effects_inc)
        self.assertIn('in.radii=engine_ring_->parent_radius;', inc)
        self.assertIn('f.filter.camera&&f.filter.scene?&f.filter:nullptr,&look,tables,f.radii,&dynamics,f.parents,f.own);', passes)
        self.assertIn('in.parents=engine_ring_->parent;', inc)
        self.assertIn('in.own=engine_ring_->own;', inc)  # after Run 127: the own ship's jets exempt from the disc law
        self.assertIn('floored=%u floor_unknown=%u unfloored=%u', inc)
        self.assertIn('if(floor_scale>=engine_plumes::floor_min&&floor_scale<=engine_plumes::floor_max)plumes_look_.floor_scale=floor_scale;', inc)
        self.assertIn('verdict=%s radius=%.6g value_eff=%.6g', effects_inc)
        self.assertIn('engine_plumes::floored_value(plumes_look_,record,jet_radius,&value_eff);', effects_inc)
        self.assertNotIn('ShipFloor', source_text(ROOT / 'src/proxy/engine_plumes_core.h'))
        self.assertIn('call<SetPsConstantsFn>(SetPixelShaderConstantF)(d,0,pixel,(12+engine_plumes::pixel_constant_floats)/4)', passes)
        self.assertIn('float4 mouth_k : register(c16);', ps)
        self.assertIn('if(!census_row){if(capture_||engine_row_frames_<engine_row_frame_cap)++engine_rows_more_;}else{',
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
        # 2 sizes x 2 skies x 3 speeds at the 15 px nozzle, and x 2 speeds at 40 px (review fix F11: the detail level 1)
        self.assertEqual(len(r['report']['resolve']), 20)
        self.assertEqual(sum(x['nozzle_px'] == 40 and x['detail'] == 1 for x in r['report']['resolve']), 8)
        self.assertEqual(len(r['report']['timing']), 6)    # 2 sizes x 30 / 100 nozzles and 300 of which 250 far (flight E)
        # After flight E: the distance law at 2 / 6 / 12 / 40 px and the dot floor, both sizes.
        self.assertEqual(len(r['report']['distance']), 8)
        self.assertTrue(all(abs(x['ratio_gpu'] - x['expected']) <= .02 * x['expected'] + .005 for x in r['report']['distance']))
        self.assertTrue(all(x['drawn_nozzle_px'] == 2.0 and x['drawn_length_px'] == 4.0 for x in r['report']['distance_dot']))
        self.assertEqual(sorted(x['far_drawn'] for x in r['report']['timing'] if x.get('far')), [250, 250])
        # The plume look (2026-10-03): alive, bulge and taper, shock cells at both sizes.
        for case in ('temporal', 'shape', 'shock'):
            self.assertEqual(len(r['report'][case]), 2, case)
        self.assertTrue(all(.05 <= x['raw_cv'] <= .4 and abs(x['mean_over_design'] - 1) <= .1 and x['raw_lag1'] >= .5
                            for x in r['report']['temporal']))
        # The body's half-width against the mock-up's law (review fixes 2026-10-03: bulge 1.15, the mock-up's tail).
        self.assertTrue(all(x['lab_w_u01'] >= 1.05 and abs(x['half_width_u01_over_nozzle_half'] / x['expected_u01'] - 1) <= .06 and
                            abs(x['half_width_u09_over_nozzle_half'] / x['expected_u09'] - 1) <= .12 for x in r['report']['shape']))
        self.assertTrue(all(x['still_maxima'] >= 3 for x in r['report']['shock']))
        # After the gap analysis (phases 2 and 3): the slots, the idle floor, the spill, the flow, the colour, the attack
        # and the travel look at both sizes.
        # The slot count is logged against an advisory 1,024 (review fix F7; AGENTS.md "Shader slot budget"), not gated.
        self.assertIsInstance(g['ps_slots'], int)
        self.assertGreater(g['ps_slots'], 0)
        self.assertEqual(g['ps_slots_within_advisory'], g['ps_slots'] <= r['gates']['ps_slots_advisory'])
        self.assertTrue(r['report']['idle'] and all(x['L_over_value'] == .5 for x in r['report']['idle']))
        # 4ch at both sizes, R32F at 1920; the nozzle 12 px (detail 0, the taper read) and 60 px (detail 1, the window's reach)
        self.assertEqual(len(r['report']['spill']), 6)
        self.assertTrue(all(abs(x['law_median'] - .15) <= .003 and x['law_max'] <= .1515 and x['law_beyond_max'] == 0 and
                            x['guard_max'] == 0 and x['law_band_px'] >= .95 * x['law_band_all'] for x in r['report']['spill']))
        self.assertTrue(all(x['law_taper_px'] > 50 and .05 <= x['law_taper_mean'] <= .1 for x in r['report']['spill'] if x['nozzle_px'] == 12))
        self.assertTrue(all(x['detail'] == 1 and x['law_taper_all'] == 0 for x in r['report']['spill'] if x['nozzle_px'] == 60))
        # The review fixes (critique section 6): the side view's energy per px^2 at 40 px 0.5..1.0 of the 12 px value (0.8 was an estimate; the hue and contrast gates win),
        # rising with the size; the end-on energy at the detail level 1 0.55..0.9 of the detail-0 law's at the same size
        # (0.7..0.9 until Run 125's soft cap 1.0 and ring x 3).
        self.assertEqual(len(r['report']['energy']), 4)
        self.assertTrue(all(x['rising'] == 1 and .5 <= x['per_px2_40_over_12'] <= 1. for x in r['report']['energy']))
        self.assertEqual(len(r['report']['end_on_detail']), 4)
        self.assertTrue(all(x['detail'] == 1 and .55 <= x['over_slab_0'] <= .9 for x in r['report']['end_on_detail']))
        self.assertEqual(len(r['report']['chase_own_look']), 2)
        self.assertEqual(len(r['report']['flow']), 8)
        self.assertTrue(all(abs(x['ratio'] - 1) <= .05 for x in r['report']['flow']))
        self.assertTrue(all(abs(x['ratio'] - 1) <= .05 for x in r['report']['flow_same']))
        self.assertTrue(len(r['report']['flow_lag']) == 4 and all(x['lag1'] >= .5 for x in r['report']['flow_lag']))
        self.assertTrue(len(r['report']['colour']) == 12 and all(x['error'] <= .01 for x in r['report']['colour']))
        attack = {(x['width'], x['kind']): x for x in r['report']['attack']}
        self.assertEqual(len(attack), 8)
        self.assertTrue(all(1.3 <= x['frame2'] <= 1.5 * 1.003 and x['back_ms'] <= 150 for k, x in attack.items()
                            if k[1] in ('steering', 'brake')))
        self.assertTrue(all(abs(x['frame2'] - 1) <= .003 for k, x in attack.items() if k[1] == 'main'))
        # The review fixes: a main jet pushed into brake flares on its first brake frame (P2); the per-nozzle phase
        # keeps a value flip's frames correlated (P1); a near plate 1.5 value in front lets at most 0.15 of the rim
        # through at value 100 and nothing at 1,000 (P4: the guard's 300-unit bound).
        self.assertTrue(len(r['report']['attack_crossing']) == 2 and all(
            1.3 <= x['frame1'] <= 1.5 * 1.003 and x['back_ms'] <= 150 for x in r['report']['attack_crossing']))
        self.assertTrue(len(r['report']['flow_keyed']) == 2 and all(
            x['lag1_keyed'] >= .5 and x['factor_changes'] == 29 for x in r['report']['flow_keyed']))
        near = r['report']['spill_near']
        self.assertEqual(len(near), 6)
        self.assertTrue(all(x['core_ratio_max'] <= .15 and x['rim_ratio_max'] <= .15 for x in near if x['value'] == 100))
        self.assertTrue(all(x['cut_max'] <= 1e-5 for x in near if x['value'] == 1000))
        self.assertTrue(len(r['report']['travel']) == 2 and all(
            x['weight'] == 1 and abs(x['L_ratio'] - 2) < 1e-3 and abs(x['I_ratio'] - 1.25) < 1e-3 and abs(x['drawn_ratio'] - 2) <= .06 and
            abs(x['peak_ratio'] - 1.25) <= .025 for x in r['report']['travel']))
        self.assertTrue(all(x['mouth_over_body'] <= .85 for x in r['report']['mouth']))
        # The revised look law (docs/architecture/engine-exhaust-look-critique.md section 5, "Implemented"): the structure
        # gates on the FP16 readback, every frame (2 sizes x 2 tints x 2 nozzles x 3 frames), and the end-on ring.
        structure = r['report']['structure']
        self.assertEqual(len(structure), 24)
        self.assertTrue(all(x['radial'] >= 3 and x['lane_depth'] >= .35 and x['gap_min'] <= .6 and x['aniso'] >= 3 and
                            x['white_axis_u05'] >= .15 and x['white_rim_u02'] >= .5 for x in structure))
        disc = r['report']['structure_disc']
        self.assertEqual(len(disc), 4)
        # The ring: red >= 1.05; cyan >= 1.0 since Run 125 (monotone under the soft cap 1.0 and the ring x 3).
        self.assertTrue(all(x['ring'] >= (1.0 if x['tint'] == 'argon-blue' else 1.05) and .4 <= x['ring_n'] <= .7 and x['hot'] >= .9
                            for x in disc))
        # Since Run 128 (Look::disc_radius 0.5): the disc's tint channel under 5 % of its centre within 0.37 natural n,
        # under 70 % within 0.25 n.
        self.assertTrue(all(0 < x['r05_n'] <= .37 and 0 < x['r70_n'] <= .25 for x in disc))
        # Since Run 125 the near-camera cap shrinks the body as before (width and length x k) while the end-on disc keeps
        # the natural nozzle width under 0.35 H: the chase case's body within the cap, its disc natural; a capital's huge
        # nozzle at 2,000 units draws its disc at its natural width and its length L x k (not the dot floor).
        self.assertEqual(len(r['report']['chase_cap']), 2)
        # Since Run 128 the disc's width is the natural one x disc_radius 0.5.
        self.assertTrue(all(x['drawn_n'] < x['natural_n'] and abs(x['disc_n'] - .5 * x['natural_n']) <= 1e-4 * x['natural_n'] and
                            abs(x['tip_body_px'] - x['cap_px']) <= .01 * x['cap_px'] and x['disc_half_px'] <= x['disc_cap_px']
                            for x in r['report']['chase_cap']))
        capital = r['report']['near_capital']
        self.assertEqual(len(capital), 2)
        self.assertTrue(all(abs(x['disc_n_px'] - .5 * x['natural_n_px']) <= 1e-3 * x['natural_n_px'] and x['capped'] == 1 and x['discs_capped'] == 0
                            and x['k'] < 1 and abs(x['drawn_length_px'] - x['k'] * x['natural_length_px']) <= .002 * x['natural_length_px']
                            and x['drawn_length_px'] > 4 for x in capital))
        kept = [x for x in r['report']['merge_kept'] if x['case'] == 'ratio_0.2']
        self.assertTrue(len(kept) == 2 and all(x['nozzles'] == 2 and x['unfloored'] == 0 for x in kept))

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
