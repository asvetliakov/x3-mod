"""Engine heat shimmer (docs/architecture/engine-exhaust-gap-analysis.md gap 9, phase 5): the portable core
src/proxy/engine_shimmer_core.h compiled on the host (the option parsers, the Ctrl+Alt+F7 latch, the amplitude law, the
rects from the plume builder: the 24 px gate, the 16-rect cap and rank, the side-view and end-on geometry, the near-plane
cut, the view filter, the occlusion depth, the mask law, the constant block and the quads), the production Ctrl+Alt+F7
block of capture.cpp executed with stubbed keys, the schema entries and launcher options, the wiring, and the tracked Wine
record of run_engine_shimmer.py bound to its production sources.
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
#include "engine_shimmer_core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::engine_shimmer;
namespace ep = x3m::engine_plumes;
namespace ee = x3m::engine_effects::core;
static unsigned failed = 0, checks = 0;
static void expect(bool ok, const char* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
static bool close_to(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static const float W = 1920.f, H = 1080.f, M11 = 1.7f, M22 = 1.000003f, M32 = -6.0000184f;
static ep::View view() { ep::View v; const float r[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; std::memcpy(v.rows, r, sizeof r);
    v.m00 = M11 * H / W; v.m11 = M11; v.height = H; v.near_z = -M32 / M22; return v; }
static Projection projection() { Projection p; p.m00 = M11 * H / W; p.m11 = M11; p.m22 = M22; p.m32 = M32; p.width = W; p.height = H; return p; }
static float ppu(float z) { return M11 * H * .5f / z; }
static ee::Record rec(float x, float y, float z, float ax, float ay, float az, float value, float zs) {
    ee::Record r{}; const float n = std::sqrt(ax * ax + ay * ay + az * az);
    r.origin[0] = x; r.origin[1] = y; r.origin[2] = z; r.axis[0] = ax / n; r.axis[1] = ay / n; r.axis[2] = az / n;
    r.size = value; r.z = zs; float s = (zs - .25f) / 1.75f; r.s = s < 0 ? 0 : s > 1 ? 1 : s; r.body = -1;
    r.flags = std::uint16_t(unsigned(ee::white) << ee::cluster_shift); r.node_handle = 7; r.model = 20000; return r;
}
// A side-view plume pointing left whose projected nozzle width is px.
static ee::Record side(float px, float x_ndc = .3f, float Z = 2000.f, float zs = 2.f) {
    const float value = px / (ep::default_look.nozzle_width * ppu(Z));
    return rec(x_ndc * Z / (M11 * H / W), 0.f, Z, -1.f, 0.f, 0.f, value, zs);
}
static unsigned collect1(const ee::Record& r, Rect* out, Stats* st, float seconds = 0.f) {
    return collect(&r, 1, nullptr, view(), projection(), ep::Preset::standard, seconds, nullptr, nullptr, nullptr, nullptr, out, st);
}
int main() {
    // ----------------------------------------------------------- options
    bool on = false;
    expect(parse_mode("on", 2, &on) && on && parse_mode(L"off", 3, &on) && !on, "on / off");
    for (const char* t : {"", "On", "ON", " on", "on ", "1", "0", "true", "of", "offf"}) { on = true; expect(!parse_mode(t, std::strlen(t), &on) && on, t); }
    float px = -1.f;
    expect(parse_px("1.5", 3, &px) && px == 1.5f && parse_px("0", 1, &px) && px == 0.f && parse_px("4", 1, &px) && px == 4.f, "px");
    for (const char* t : {"4.1", "-1", "1e0", "nan", "", "1.5x", "+1"}) { px = 9.f; expect(!parse_px(t, std::strlen(t), &px) && px == 9.f, t); }
    unsigned most = 99;
    expect(parse_max("4", 1, &most) && most == 4 && parse_max(L"16", 2, &most) && most == 16 && parse_max("0", 1, &most) && most == 0, "max");
    for (const char* s : {"17", "", "-1", "4.0", "016", " 4", "a"}) { most = 99; expect(!parse_max(s, std::strlen(s), &most) && most == 99, s); }
    expect(default_max == 4 && max_rects == 16, "default 4 of 16");
    float amp = 0.f;
    amplitude_px(1.5f, 1440.f, &amp); expect(amp == 1.5f, "1.5 px at 1440 rows");
    amplitude_px(1.5f, 1080.f, &amp); expect(close_to(amp, 1.125f, 1e-6f), "scaled with the height");
    amplitude_px(0.f, 1080.f, &amp); expect(amp == 0.f, "zero");
    // ----------------------------------------------------------- the F7 latch
    {
        ToggleKey k; unsigned n = 0;
        auto f = [&](bool focus, bool c, bool a, bool s, bool f7) { n += k.step(focus, c, a, s, f7); };
        for (int i = 0; i < 5; ++i) f(true, true, true, false, true);
        expect(n == 1, "held F7 is one press");
        f(true, true, true, false, false); f(true, true, true, false, true); expect(n == 2, "second press");
        f(true, true, true, false, false); f(true, true, true, true, true); expect(n == 2, "Shift held: no press");
        f(true, false, false, false, false); f(true, false, false, false, true); f(true, true, true, false, true); expect(n == 2, "F7 before the modifiers");
        f(false, false, false, false, false); f(false, true, true, false, true); f(true, true, true, false, true); expect(n == 2, "unfocused");
    }
    // ----------------------------------------------------------- the gate and the side view
    {
        Rect r[max_rects]; Stats st{};
        expect(collect1(side(20.f), r, &st) == 0 && st.small == 1, "20 px: none");
        expect(collect1(side(23.9f), r, &st) == 0 && st.small == 1, "23.9 px: none");
        expect(collect1(side(24.5f), r, &st) == 1 && st.kept == 1, "24.5 px: one");
        const ee::Record s = side(60.f);
        expect(collect1(s, r, &st) == 1, "60 px: one");
        const Rect& q = r[0];
        expect(close_to(q.nozzle_px, 60.f, .05f) && close_to(q.half_width, 60.f, .05f) && close_to(q.back, 15.f, .02f), "nozzle, half-width, back");
        expect(close_to(q.axis[0], -1.f, 1e-5f) && close_to(q.axis[1], 0.f, 1e-5f), "axis left");
        // The origin: the nozzle's projection at continuous pixel coordinates.
        expect(close_to(q.origin[0], (.3f + 1.f) * W * .5f + .5f, .01f) && close_to(q.origin[1], H * .5f + .5f, .01f), "origin");
        // The length: 1.5 x the builder's pulsed L (z value x pulse) projected; the pulse is 1 +- 0.25.
        const float value = s.size, L_nominal = 2.f * value * ppu(2000.f);
        expect(q.length >= 1.5f * .75f * L_nominal - 1.f && q.length <= 1.5f * 1.25f * L_nominal + 1.f, "length 1.5 L");
        expect(q.depth > 0.f && q.depth < 1.f && close_to(q.depth, M22 + M32 / (2000.f - .5f * value), 1e-6f), "occlusion depth one nozzle width nearer");
        // The fade (review S3): over half a nozzle width of view depth in front of that, as 1 / its device-depth span.
        const float d_full = M22 + M32 / (2000.f - .5f * value - .25f * value);
        expect(close_to(q.fade, 1.f / (q.depth - d_full), 1e-3f) && q.fade > 0.f, "occlusion fade over 0.5 nozzle widths of depth");
        expect(q.bounds[0] >= 0 && q.bounds[2] <= int(W) && q.bounds[3] - q.bounds[1] == int(std::ceil(q.origin[1] + 61.f)) - int(std::floor(q.origin[1] - 61.f)), "box");
        // The mask: 1 near the nozzle on the centre line, 0 on every border, 0 outside.
        float m = 0.f;
        mask_at(q, q.origin[0] - .05f * q.length, q.origin[1], &m); expect(m > .97f, "near the nozzle");
        mask_at(q, q.origin[0] - .5f * q.length, q.origin[1], &m); expect(close_to(m, .5f, .01f), "half-way");
        mask_at(q, q.origin[0] - q.length, q.origin[1], &m); expect(m < 1e-4f, "the tip");
        mask_at(q, q.origin[0] + q.back, q.origin[1], &m); expect(m < 1e-4f, "behind");
        mask_at(q, q.origin[0] + .5f * q.back, q.origin[1], &m); expect(close_to(m, .5f, .01f), "the fade-in");
        mask_at(q, q.origin[0] - .2f * q.length, q.origin[1] + q.half_width, &m); expect(m < 1e-4f, "the side");
        mask_at(q, q.origin[0] - .2f * q.length, q.origin[1] + .3f * q.half_width, &m); expect(m > .8f, "inside the side fade");
        mask_at(q, q.origin[0] + 500.f, q.origin[1], &m); expect(m == 0.f, "outside");
    }
    // ----------------------------------------------------------- end-on, behind, near cut, offscreen, filter
    {
        Rect r[max_rects]; Stats st{};
        const float Z = 2000.f, value = 60.f / (.5f * ppu(Z));
        // The exhaust pointing at the camera: the rect centred on the nozzle (back = half-width - length).
        expect(collect1(rec(0, 0, Z, 0, 0, -1, value, 2.f), r, &st) == 1 && r[0].length == r[0].half_width &&
               close_to(r[0].back, r[0].half_width, .01f), "end-on centred");
        expect(r[0].depth > 0.f && r[0].depth < M22 + M32 / (Z - 2.f * value), "end-on: the tip is the nearest point");
        expect(collect1(rec(0, 0, -50.f, -1, 0, 0, value, 2.f), r, &st) == 0 && st.behind == 1, "behind the camera");
        // A long plume running towards the camera past the near plane: cut there, the length bounded.
        expect(collect1(rec(0, 0, 40.f, 0, .1f, -1, 6.f, 2.f), r, &st) == 1 && r[0].length <= 4.f * W, "near cut");
        expect(collect1(rec(-20000.f, 0, Z, -1, 0, 0, value, 2.f), r, &st) == 0 && st.offscreen == 1, "offscreen");
        const ee::Record two[2] = {side(60.f, .3f), side(40.f, -.3f)};
        const std::uint32_t camera[2] = {5, 6}; const std::uint8_t scene[2] = {1, 1};
        ep::ViewFilter filter{camera, scene, 6};
        expect(collect(two, 2, nullptr, view(), projection(), ep::Preset::standard, 0.f, &filter, nullptr, nullptr, nullptr, r, &st) == 1 &&
               close_to(r[0].nozzle_px, 40.f, .05f) && st.candidates == 1, "the scene view's records only");
    }
    // ----------------------------------------------------------- the stage's dynamics: travel length, per-nozzle phase
    {
        Rect a[max_rects], b[max_rects]; Stats st{};
        const ee::Record s = side(30.f, .6f, 2000.f, .5f); // a short plume: its 1.5 L stays on screen at travel 1
        ep::Dynamics still{}, travel{};
        still.flow = travel.flow = 123.25;
        travel.travel = 1.f;
        ep::Transients* never = nullptr; // the stage's attack memory is not passed through
        travel.transients = never;
        const unsigned n0 = collect(&s, 1, nullptr, view(), projection(), ep::Preset::standard, 0.f, nullptr, nullptr, nullptr, nullptr, a, &st, 4, &still);
        const unsigned n1 = collect(&s, 1, nullptr, view(), projection(), ep::Preset::standard, 0.f, nullptr, nullptr, nullptr, nullptr, b, &st, 4, &travel);
        expect(n0 == 1 && n1 == 1 && close_to(b[0].length / a[0].length, ep::travel_length, .02f), "travel lengthens the rect as the plume");
        float factor = 0.f, phase = 0.f;
        ep::flow_factor(ep::default_look, s.size, &factor);
        ep::nozzle_phase(123.25, factor, &phase);
        expect(a[0].phase == phase && phase > 0.f, "the nozzle's own flow phase (no memory: the shared phase)");
        // Review P1: with the stage's per-nozzle memory the rect reads the nozzle's own accumulated phase (read only:
        // the memory is unchanged), advanced to this frame's accumulator.
        static ep::Transients memory; memory.clear(); memory.begin(1.f / 60.f);
        ep::Transients::Slot* slot = memory.touch(ep::identity_key(s));
        float stage_phase = 0.f;
        memory.advance_phase(*slot, 100., factor, &stage_phase); // the stage's earlier frame, then 23.25 widths on
        slot->phase = 777.;
        ep::Dynamics keyed = still;
        keyed.transients = &memory;
        const std::uint32_t frame_before = memory.frame;
        const double kept = slot->phase;
        collect(&s, 1, nullptr, view(), projection(), ep::Preset::standard, 0.f, nullptr, nullptr, nullptr, nullptr, b, &st, 4, &keyed);
        expect(close_to(b[0].phase, float(777. + 23.25 * double(factor)), 1e-4f) && slot->phase == kept && memory.frame == frame_before,
               "the rect takes the nozzle's own phase from the memory, read only");
    }
    // ----------------------------------------------------------- the cap and the rank
    {
        std::vector<ee::Record> crowd;
        for (int i = 0; i < 20; ++i) crowd.push_back(side(30.f + float((i * 7) % 20), -.6f + .06f * float(i)));
        Rect r[max_rects]; Stats st{};
        const unsigned n = collect(crowd.data(), unsigned(crowd.size()), nullptr, view(), projection(), ep::Preset::standard, 0.f,
                                   nullptr, nullptr, nullptr, nullptr, r, &st);
        bool ranked = n == 16 && st.capped == 4 && st.kept == 16;
        for (unsigned i = 1; i < n; ++i) ranked = ranked && r[i - 1].nozzle_px >= r[i].nozzle_px;
        expect(ranked && close_to(r[0].nozzle_px, 49.f, .05f) && close_to(r[15].nozzle_px, 34.f, .05f), "16 largest, largest first");
        const unsigned four = collect(crowd.data(), unsigned(crowd.size()), nullptr, view(), projection(), ep::Preset::standard, 0.f,
                                      nullptr, nullptr, nullptr, nullptr, r, &st, default_max);
        expect(four == 4 && st.capped == 16 && close_to(r[0].nozzle_px, 49.f, .05f) && close_to(r[3].nozzle_px, 46.f, .05f), "the default limit: 4 largest");
        expect(collect(crowd.data(), unsigned(crowd.size()), nullptr, view(), projection(), ep::Preset::standard, 0.f,
                       nullptr, nullptr, nullptr, nullptr, r, &st, 0) == 0, "limit 0: none");
    }
    // ----------------------------------------------------------- constants, quads, union
    {
        Rect r[max_rects]; Stats st{};
        collect1(side(60.f), r, &st);
        float c[constant_vectors * 4];
        r[0].phase = 8.f;
        constants(r, 1, 1.125f, 3.f, W, H, c);
        expect(c[(4 + 32) * 4 + 2] == 8.f / cell_widths, "the rect's own flow phase in c36+i.z");
        expect(close_to(c[0], 1.f / W, 1e-9f) && c[2] == W && c[3] == H && c[4] == 1.125f && c[5] == 1.f && c[6] == 0.f &&
               c[7] == 3.f * boil_rate && c[8] == 1.f / cell_widths, "c0..c2");
        expect(c[16] == r[0].origin[0] && c[18] == r[0].axis[0] && c[(4 + 16) * 4] == r[0].length && c[(4 + 16) * 4 + 1] == r[0].half_width &&
               c[(4 + 16) * 4 + 2] == r[0].back && close_to(c[(4 + 16) * 4 + 3], 1.f / r[0].nozzle_px, 1e-9f) &&
               c[(4 + 32) * 4 + 1] == r[0].depth && c[(4 + 32) * 4 + 3] == r[0].fade && c[(4 + 1) * 4] == 0.f,
               "per-rect registers c4 / c20 / c36 (c36.w the occlusion fade)");
        QuadVertex v[vertices_per_rect];
        vertices(r, 1, W, H, v);
        // Continuous pixel X maps to clip 2 X / W - 1 - 1 / W and u X / W (pixel i's centre interpolates (i + 0.5) / W).
        const float* c0 = r[0].corners[0];
        expect(close_to(v[0].x, 2.f * c0[0] / W - 1.f - 1.f / W, 1e-6f) && close_to(v[0].y, 1.f - 2.f * c0[1] / H + 1.f / H, 1e-6f) &&
               close_to(v[0].u, c0[0] / W, 1e-7f) && v[0].w == 1.f && v[3].x == v[2].x && v[4].x == v[1].x, "quad vertices");
        int u[4];
        expect(union_bounds(r, 1, 3, int(W), int(H), u) && u[0] == r[0].bounds[0] - 3 && u[2] == r[0].bounds[2] + 3, "union with margin");
        Rect edge = r[0]; edge.bounds[0] = 0; edge.bounds[1] = 0;
        expect(union_bounds(&edge, 1, 5, int(W), int(H), u) && u[0] == 0 && u[1] == 0, "clipped to the target");
    }
    // ----------------------------------------------------------- the rect build's cost
    {
        // 300 records, 30 of them 24..90 px nozzles, the rest 1..20 px (the far crowd), random axes and throttles.
        std::vector<ee::Record> crowd; std::uint32_t seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.f; };
        for (int i = 0; i < 300; ++i) {
            const float z = 1500.f + 28500.f * rnd() * rnd(), x = (rnd() * 1.6f - .8f) * z / (M11 * H / W), y = (rnd() * 1.6f - .8f) * z / M11;
            const float px = i < 30 ? 24.f + 66.f * rnd() : 1.f + 19.f * rnd();
            crowd.push_back(rec(x, y, z, rnd() - .5f, rnd() - .5f, rnd() - .5f, px / (.5f * ppu(z)), .25f + 1.75f * rnd()));
        }
        std::vector<float> radii(crowd.size(), 300.f);
        const ep::Look look = ep::default_look; ep::LookTables tables; ep::look_tables(look, &tables);
        Rect r[max_rects]; Stats st{}; unsigned kept = 0;
        std::vector<double> us;
        for (int round = 0; round < 101; ++round) {
            const auto a = std::chrono::steady_clock::now();
            kept = collect(crowd.data(), unsigned(crowd.size()), nullptr, view(), projection(), ep::Preset::standard, 1.f, nullptr,
                           &look, &tables, radii.data(), r, &st);
            us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count());
        }
        std::sort(us.begin(), us.end());
        expect(kept >= 10 && kept <= 16, "the crowd keeps its near nozzles");
        std::printf("COLLECT records=300 kept=%u small=%u median_us=%.2f\n", kept, st.small, us[50]);
    }
    std::printf("engine_shimmer_core checks=%u failed=%u\n", checks, failed);
    return failed ? 1 : 0;
}
'''


def compiler():
    found = shutil.which('clang++') or shutil.which('c++')
    if found is None:
        raise unittest.SkipTest('A host C++ compiler is required')
    return found


class EngineShimmerCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory(prefix='x3-engine-shimmer-host-') as directory:
            source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'engine_shimmer_host'
            source.write_text(HARNESS)
            built = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
        cls.returncode, cls.lines = run.returncode, run.stdout.splitlines()

    def test_checks_pass(self):
        self.assertEqual(self.returncode, 0, '\n'.join(l for l in self.lines if l.startswith('FAIL')))
        self.assertRegex(self.lines[-1], r'^engine_shimmer_core checks=\d+ failed=0$')

    def test_collect_cost(self):
        row = next(m for l in self.lines if (m := re.match(r'COLLECT records=300 kept=(\d+) small=(\d+) median_us=([\d.]+)', l)))
        self.assertLess(float(row.group(3)), 100.0)  # host clang -O2: the rect build for 300 records


F7_BEGIN = '    // Ctrl+Alt+F7 (comparison-hotkeys.md'
F7_END = 'ctx.motion_output.engine_shimmer_toggle();\n    }\n'


def f7_block(capture):
    start = capture.index(F7_BEGIN)
    return capture[start:capture.end(F7_END, start)]


class ToggleHotkey(unittest.TestCase):
    """The production Ctrl+Alt+F7 block of capture.cpp's Present, executed with stubbed keys and focus."""

    def test_block(self):
        block = f7_block(source_text(ROOT / 'src/proxy/capture.cpp'))
        harness = textwrap.dedent('''
            #include "engine_shimmer_core.h"
            #include <cstdio>
            #define VK_F7 0x76
            #define VK_CONTROL 0x11
            #define VK_MENU 0x12
            #define VK_SHIFT 0x10
            static unsigned polls = 0, checks = 0, failures = 0;
            static bool keys[256], focused = true;
            short GetAsyncKeyState(int key) { ++polls; return keys[key & 255] ? short(-32768) : short(0); }
            bool comparison_foreground() noexcept { return focused; }
            namespace x3m {
            struct Motion { bool requested = false; unsigned toggles = 0; bool engine_shimmer_requested() const { return requested; }
                            int engine_shimmer_toggle() { ++toggles; return 0; } };
            struct Ctx { Motion motion_output; engine_shimmer::ToggleKey shimmer_key; };
            static void frame(Ctx& ctx) {
            @BLOCK@
            }
            }
            #define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line=%d %s\\n", __LINE__, #x); } } while (0)
            static void set(bool c, bool a, bool s, bool f7) { keys[VK_CONTROL] = c; keys[VK_MENU] = a; keys[VK_SHIFT] = s; keys[VK_F7] = f7; }
            int main() {
                x3m::Ctx ctx;
                set(true, true, false, true);
                for (int i = 0; i < 5; ++i) x3m::frame(ctx);
                CHECK(polls == 0 && ctx.motion_output.toggles == 0); // not requested: never polled
                ctx.motion_output.requested = true;
                set(false, false, false, false); polls = 0;
                for (int i = 0; i < 10; ++i) x3m::frame(ctx);
                CHECK(polls == 10); // idle: F7 only, once per frame
                set(true, true, false, true); for (int i = 0; i < 6; ++i) x3m::frame(ctx);
                CHECK(ctx.motion_output.toggles == 1); // a held chord is one press
                set(true, true, false, false); x3m::frame(ctx); set(true, true, false, true); x3m::frame(ctx);
                CHECK(ctx.motion_output.toggles == 2);
                set(true, true, true, false); x3m::frame(ctx); set(true, true, true, true); x3m::frame(ctx);
                CHECK(ctx.motion_output.toggles == 2); // Shift held
                set(false, false, false, false); x3m::frame(ctx); set(false, false, false, true); x3m::frame(ctx); set(true, true, false, true); x3m::frame(ctx);
                CHECK(ctx.motion_output.toggles == 2); // F7 first, modifiers later
                set(false, false, false, false); x3m::frame(ctx); focused = false; set(true, true, false, true); x3m::frame(ctx);
                focused = true; x3m::frame(ctx);
                CHECK(ctx.motion_output.toggles == 2); // unfocused press, focus returns while held
                std::printf("f7 checks=%u failures=%u\\n", checks, failures);
                return failures ? 1 : 0;
            }
        ''').replace('@BLOCK@', str(block))
        with tempfile.TemporaryDirectory(prefix='x3-f7-') as temporary:
            source, executable = Path(temporary) / 'f7.cpp', Path(temporary) / 'f7'
            source.write_text(harness)
            built = subprocess.run([compiler(), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-I', str(ROOT / 'src/proxy'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'f7 checks=7 failures=0\n')


class Options(unittest.TestCase):
    def test_schema_entries(self):
        e = schema.BY_KEY['engine_shimmer']
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['choices'], e['launcher'], e['developer']),
                         ('X3M_ENGINE_SHIMMER', 'enum', 'engine', None, 'on', ('on', 'off'), '--engine-shimmer', False))
        p = schema.BY_KEY['engine_shimmer_px']
        self.assertEqual((p['env'], p['type'], p['default'], p['builtin'], p['launcher']),
                         ('X3M_ENGINE_SHIMMER_PX', 'float', None, '1.5', '--engine-shimmer-px'))
        self.assertEqual(p['range'], ((0.0, 4.0, False),))
        m = schema.BY_KEY['engine_shimmer_max']
        self.assertEqual((m['env'], m['type'], m['default'], m['builtin'], m['launcher']),
                         ('X3M_ENGINE_SHIMMER_MAX', 'int', None, '4', '--engine-shimmer-max'))
        self.assertEqual(m['range'], ((0.0, 16.0, False),))
        header = (ROOT / 'src/config/config_schema_inc.h').read_text()
        self.assertIn('{"X3M_ENGINE_SHIMMER", "engine_shimmer", Type::Enum, nullptr,', header)
        self.assertIn('{"X3M_ENGINE_SHIMMER_PX", "engine_shimmer_px", Type::Float, nullptr,', header)
        template = (ROOT / 'assets/x3m.ini').read_text()
        self.assertIn(';engine_shimmer = on\n', template)
        self.assertIn(';engine_shimmer_px = 1.5\n', template)
        self.assertIn(';engine_shimmer_max = 4\n', template)
        self.assertIn('{"X3M_ENGINE_SHIMMER_MAX", "engine_shimmer_max", Type::Int, nullptr,', header)

    def test_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            env = launch_env(module, game, wine, '--engine-effects', 'plumes')
            self.assertNotIn('X3M_ENGINE_SHIMMER', env)
            self.assertNotIn('X3M_ENGINE_SHIMMER_PX', env)
            for value in ('on', 'off'):
                self.assertEqual(launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-shimmer', value)['X3M_ENGINE_SHIMMER'], value)
            for value, sent in (('1.5', '1.5'), ('0', '0'), ('4', '4'), ('2.25', '2.25')):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-shimmer-px', value)
                self.assertEqual(env['X3M_ENGINE_SHIMMER_PX'], sent)
            self.assertNotIn('X3M_ENGINE_SHIMMER_MAX', launch_env(module, game, wine, '--engine-effects', 'plumes'))
            for value in ('0', '4', '16'):
                self.assertEqual(launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-shimmer-max', value)['X3M_ENGINE_SHIMMER_MAX'], value)
            for bad in ('-1', '17'):
                with self.assertRaises(SystemExit):
                    launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-shimmer-max', bad)
            for bad in ('-0.1', '4.1', 'nan', 'inf'):
                with self.assertRaises(SystemExit):
                    launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-shimmer-px', bad)
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--engine-shimmer', 'Off')
            for option, value in (('--engine-shimmer', 'off'), ('--engine-shimmer-px', '1'), ('--engine-shimmer-max', '4')):
                with self.assertRaises(SystemExit):
                    launch_env(module, game, wine, '--vanilla', option, value)

    def test_read_once_per_device_configure(self):
        inc = (ROOT / 'src/proxy/motion_output_engine_shimmer_inc.h').read_text()
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_SHIMMER"', inc)), 1)
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_SHIMMER_PX"', inc)), 1)
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_SHIMMER_MAX"', inc)), 1)
        self.assertIn('shimmer_requested_ = plumes_requested_ && on && px > 0.f && limit > 0;', inc)
        self.assertIn('&shimmer_stats_, shimmer_max_, &dynamics);', inc)
        self.assertIn('dynamics.travel = engine_travel_weight_;', inc)


class Wiring(unittest.TestCase):
    def test_wiring(self):
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('#include "motion_output_engine_shimmer_inc.h"', motion)
        # The post-resolve site: the FP16 route's resolved image, before the write-back (end_redirect) and bloom read it.
        # The order is also executed (review S1): the effects fixture's armed mode hashes frame N's resolved image
        # before and after the shimmer and after its revert, and frame N+1's resolve reads it as history
        # (test_engine_effects EngineEffectsFixtureRecord, `shimmer_history`).
        resolve = motion[motion.index('HRESULT MotionOutput::resolve(IDirect3DSurface9*'):motion.index('bool MotionOutput::resolve_hdr(')]
        site = resolve.index('run_engine_shimmer(out.color,out.color_surface,depth,in.width,in.height);')
        self.assertLess(resolve.index('hdr_resolved_=out.color;'), site)
        self.assertLess(resolve.index('hr=taa_->run(in,&out);'), site)
        # Before Present: after the terminal end_redirect of the frame, before Present.
        present = motion[motion.index('void MotionOutput::before_present()'):]
        self.assertLess(present.index('end_redirect(HdrEnd::Present);'), present.index('revert_engine_shimmer();'))
        self.assertIn('engine_shimmer_before_reset();', motion)
        self.assertIn('engine_shimmer_after_reset(result);', motion)
        self.assertIn('release_engine_shimmer();', motion)
        self.assertIn('if(engine_hook_)log_engine_shimmer();', motion)
        self.assertIn('"engine_shimmer"};', motion)
        self.assertIn('hooked.motion_output.configure_engine_shimmer();', capture)
        self.assertLess(capture.index('configure_engine_plumes('), capture.index('configure_engine_shimmer()'))
        self.assertIn('src/renderer/engine_shimmer_pass.cpp', (ROOT / 'CMakeLists.txt').read_text())


class EngineShimmerFixtureRecord(unittest.TestCase):
    """The tracked Wine record of run_engine_shimmer.py, bound to the production sources it exercised."""
    PATH = ROOT / 'verification/results/bottle-X3/engine-shimmer/summary.json'

    def setUp(self):
        self.record = json.loads(self.PATH.read_text())

    def test_passed(self):
        r = self.record
        self.assertTrue(r['passed'])
        self.assertFalse(r['game_launched'])
        self.assertEqual(r['bottle']['name'], 'X3')
        self.assertEqual(r['report']['failed_checks'], [])
        self.assertEqual(r['build']['warnings'], 0)
        self.assertEqual(len(r['report']['displace']), 2)
        for x in r['report']['displace']:
            self.assertLessEqual(x['max_px'], x['amplitude_px'] + .05)
            self.assertEqual((x['outside_diff'], x['band_diff'], x['restored']), (0, 0, 1))
        self.assertEqual(sorted((x['width'], x['rects']) for x in r['report']['timing']),
                         [(1920, 1), (1920, 4), (1920, 16), (5120, 1), (5120, 4), (5120, 16)])

    def test_bound_to_its_production_sources(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        import run_engine_shimmer as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}(-dirty)?$')
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_engine_shimmer.py')


if __name__ == '__main__':
    unittest.main()
