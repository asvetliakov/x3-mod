"""Host checks of the bolt footprint (src/proxy/bolt_footprint_core.h,
docs/architecture/bolt-footprint.md option A').

The core compiled from its header on the host: the instance period rule on
synthetic UV streams (found for 24/84/234-vertex bodies, refused for a
remapped stream, a count that is no multiple of 3, a body past max_period), the
expansion of instances whose projected half-extent lies below, between and
above R/G against an independent double-precision projection of the written
vertices (half-extents reach their targets, continuity at the gate, clip z and w
unchanged, an instance behind the camera plane refused, a bolt of 2G px or
more untouched byte for byte), the rows test, the widened Unlock scan
(locked_prefix_core.h keeps the UV/colour words), the proxy wiring and the
--bolt-footprint launcher gates (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/bolt_footprint_core.h'
R_DEFAULT, G_DEFAULT = 3.0, 8.0
W_EPSILON = 1e-3
EXTENT_FLOOR = 0.05

HARNESS = r'''
#include "bolt_footprint_core.h"
#include "locked_prefix_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace x3m::bolt_footprint;
namespace prefix = x3m::fade_region::prefix;

static std::vector<std::uint32_t> uv_stream(unsigned instances, unsigned body, bool remap, unsigned sub_period) {
    // extras: 3 words per vertex; the body's UV pattern repeats per instance
    // (sub_period: the body itself repeats a shorter pattern, a symmetric body).
    std::vector<std::uint32_t> out(std::size_t(instances) * body * 3);
    for (unsigned i = 0; i < instances; ++i)
        for (unsigned v = 0; v < body; ++v) {
            const unsigned k = sub_period ? v % sub_period : v;
            std::uint32_t* w = out.data() + (std::size_t(i) * body + v) * 3;
            w[0] = 0x3f000000u + k * 0x1000u + (remap ? i * 0x10u : 0u);
            w[1] = 0x3e800000u + k * 0x2000u;
            w[2] = 0xff000000u | i;
        }
    return out;
}

static int period_cases() {
    struct Case { const char* name; unsigned instances, body; bool remap; unsigned sub; unsigned count_override; };
    const Case cases[] = {
        {"point_sing_x2", 2, 24, false, 0, 0}, {"energy_bolt_x3", 3, 84, false, 0, 0}, {"repeat_x2", 2, 234, false, 0, 0},
        {"single_84", 1, 84, false, 0, 0}, {"third_person_33x72", 33, 72, false, 0, 0}, {"remapped_x3", 3, 84, true, 0, 0},
        {"not_multiple_of_3", 1, 85, false, 0, 0}, {"too_short_21", 1, 21, false, 0, 0}, {"past_max_period_300", 1, 300, false, 0, 0},
        {"symmetric_84_sub42", 2, 84, false, 42, 0}, {"truncated_count", 3, 84, false, 0, 250}, {"single_255", 1, 255, false, 0, 0},
        {"single_234", 1, 234, false, 0, 0}, {"remapped_x2_168", 2, 84, true, 0, 0},
        {"repeat_x2_sub78", 2, 234, false, 78, 0}, {"repeat_x1_sub78", 1, 234, false, 78, 0}, {"mod_78_x2", 2, 78, false, 0, 0},
        {"plasma_x3_sub24", 3, 72, false, 24, 0},
    };
    for (const Case& c : cases) {
        const auto s = uv_stream(c.instances, c.body, c.remap, c.sub);
        const std::uint32_t count = c.count_override ? c.count_override : c.instances * c.body;
        std::printf("PERIOD %s %u\n", c.name, unsigned(detect_period(s.data(), count)));
    }
    std::printf("PERIOD null 0 %u\n", unsigned(detect_period(nullptr, 84)));
    return 0;
}

static int scan_case() {
    // 40 written vertices then a sentinel vertex then garbage: the scan must
    // copy positions and the other three words of exactly the 40.
    const unsigned n = 40;
    std::vector<unsigned char> window(std::size_t(n + 8) * prefix::stride, 0x5a);
    std::vector<float> positions(prefix::storage_floats); std::vector<std::uint32_t> extras(prefix::storage_words);
    for (unsigned i = 0; i < n; ++i) {
        float p[3] = {float(i) * 1.5f, -float(i), 100.f + float(i)};
        std::uint32_t w[3] = {0x3f800000u + i, 0x3e000000u + i * 7u, 0x80ff0000u | i};
        std::memcpy(window.data() + std::size_t(i) * prefix::stride, p, 12);
        std::memcpy(window.data() + std::size_t(i) * prefix::stride + 12, w, 12);
    }
    std::memset(window.data() + std::size_t(n) * prefix::stride, 0xff, 12);
    prefix::Scan out{};
    const std::uint32_t scanned = prefix::scan(window.data(), window.size(), positions.data(), &out, extras.data());
    bool ok = scanned == n && !out.window_end && out.bad_from == ~0u;
    for (unsigned i = 0; i < n && ok; ++i) {
        ok = std::memcmp(positions.data() + i * 3, window.data() + std::size_t(i) * prefix::stride, 12) == 0
          && std::memcmp(extras.data() + i * 3, window.data() + std::size_t(i) * prefix::stride + 12, 12) == 0;
    }
    // The table path: mark, lock (sentinel), unlock (scan), lookup with extras.
    prefix::Table t; ok = ok && t.mark(7);
    std::vector<unsigned char> mapping(std::size_t(prefix::max_vertices) * prefix::stride, 0);
    t.begin_lock(7, true, mapping.data(), mapping.size(), 1);
    std::memcpy(mapping.data(), window.data(), std::size_t(n) * prefix::stride);
    const std::uint32_t published = t.finish_lock(7, 1);
    const float* lp = nullptr; const std::uint32_t* le = nullptr; std::uint64_t rev = 0; std::uint32_t sc = 0;
    const auto look = t.lookup(7, n, &lp, &rev, &sc, &le);
    ok = ok && published == n && look == prefix::Lookup::Bound && lp && le && sc == n
        && std::memcmp(le, extras.data(), std::size_t(n) * 12) == 0 && std::memcmp(lp, positions.data(), std::size_t(n) * 12) == 0;
    std::printf("SCAN scanned=%u published=%u ok=%u\n", unsigned(scanned), unsigned(published), unsigned(ok));
    return 0;
}

static int draw_case(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return 2;
    float rows[16]; unsigned vx, vy, vw, vh; float r_px, g_px; unsigned count;
    for (float& v : rows) if (std::fscanf(f, "%f", &v) != 1) return 3;
    if (std::fscanf(f, "%u %u %u %u", &vx, &vy, &vw, &vh) != 4) return 3;
    if (std::fscanf(f, "%f %f", &r_px, &g_px) != 2) return 3;
    if (std::fscanf(f, "%u", &count) != 1) return 3;
    std::vector<float> positions(std::size_t(count) * 3); std::vector<std::uint32_t> extras(std::size_t(count) * 3);
    for (unsigned i = 0; i < count; ++i) {
        float u, v; unsigned colour;
        if (std::fscanf(f, "%f %f %f %f %f %u", &positions[i * 3], &positions[i * 3 + 1], &positions[i * 3 + 2], &u, &v, &colour) != 6) return 3;
        std::memcpy(&extras[i * 3], &u, 4); std::memcpy(&extras[i * 3 + 1], &v, 4); extras[i * 3 + 2] = colour;
    }
    std::fclose(f);
    Frame frame;
    const FrameReason reason = prepare_frame(rows, vx, vy, vw, vh, &frame);
    std::printf("FRAME %s\n", frame_reason_name(reason));
    std::vector<Plan> plans(max_instances);
    DrawStats stats;
    const bool planned = plan_draw(frame, positions.data(), extras.data(), count, r_px, g_px, plans.data(), max_instances, &stats);
    std::printf("STATS ok=%u period=%u instances=%u expanded=%u untouched=%u refused_w=%u nonfinite=%u\n", unsigned(planned), stats.period, stats.instances, stats.expanded, stats.untouched, stats.refused_w, stats.nonfinite);
    if (!planned) return 0;
    for (unsigned i = 0; i < stats.instances; ++i) {
        const Plan& p = plans[i];
        std::printf("PLAN %u verdict=%u a=%a b=%a s1=%a s2=%a qc=%a,%a e1=%a,%a\n", i, unsigned(p.verdict), p.a, p.b, p.s1, p.s2, p.qc[0], p.qc[1], p.e1[0], p.e1[1]);
    }
    std::vector<unsigned char> out(std::size_t(count) * stride, 0xcd);
    const std::uint32_t written = write_draw(frame, positions.data(), extras.data(), count, stats.period, plans.data(), out.data());
    std::printf("WRITTEN %u\n", unsigned(written));
    for (unsigned i = 0; i < count; ++i) {
        float p[3]; std::uint32_t w[3];
        std::memcpy(p, out.data() + std::size_t(i) * stride, 12); std::memcpy(w, out.data() + std::size_t(i) * stride + 12, 12);
        const bool same = std::memcmp(out.data() + std::size_t(i) * stride, &positions[i * 3], 12) == 0 && std::memcmp(w, &extras[i * 3], 12) == 0;
        std::printf("OUT %u %a %a %a %08x %08x %08x %u\n", i, p[0], p[1], p[2], w[0], w[1], w[2], unsigned(same));
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    if (!std::strcmp(argv[1], "period")) return period_cases();
    if (!std::strcmp(argv[1], "scan")) return scan_case();
    if (!std::strcmp(argv[1], "draw") && argc > 2) return draw_case(argv[2]);
    if (!std::strcmp(argv[1], "params")) {
        std::printf("PARAMS %u %u %u %u %u %u %u\n", unsigned(valid_parameters(3.f, 8.f)), unsigned(valid_parameters(0.f, 8.f)), unsigned(valid_parameters(8.f, 8.f)),
                    unsigned(valid_parameters(64.f, 256.f)), unsigned(valid_parameters(65.f, 256.f)), unsigned(valid_parameters(3.f, 257.f)), unsigned(valid_parameters(3.f, 0.f / 0.f)));
        return 0;
    }
    return 1;
}
'''


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


# ---- projection oracle (double precision, independent of the header) ----

def perspective(fov_y, aspect, zn, zf):
    t = 1.0 / math.tan(fov_y / 2)
    return [[t / aspect, 0, 0, 0], [0, t, 0, 0], [0, 0, zf / (zf - zn), -zn * zf / (zf - zn)], [0, 0, 1, 0]]


def rotation(yaw, pitch, roll):
    cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
    ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    rx = [[1, 0, 0], [0, cp, -sp], [0, sp, cp]]
    rz = [[cr, -sr, 0], [sr, cr, 0], [0, 0, 1]]
    return mat3(mat3(rz, rx), ry)


def mat3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def view_matrix(camera, rot):
    # view = R (p - c): rows of R are the camera's right / up / forward axes.
    return [[rot[i][0], rot[i][1], rot[i][2], -sum(rot[i][k] * camera[k] for k in range(3))] for i in range(3)] + [[0, 0, 0, 1]]


def mat4(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def rows_of(m):
    return [f32(m[r][c]) for r in range(4) for c in range(4)]


def clip(rows, p):
    return [rows[4 * r] * p[0] + rows[4 * r + 1] * p[1] + rows[4 * r + 2] * p[2] + rows[4 * r + 3] for r in range(4)]


def pixel(rows, vp, p):
    c = clip(rows, p)
    x, y, w, h = vp
    return (x + (c[0] / c[3] + 1) * w / 2, y + (1 - c[1] / c[3]) * h / 2), c[3]


def extents(points, e1):
    n = len(points)
    cx, cy = sum(p[0] for p in points) / n, sum(p[1] for p in points) / n
    a = max(abs((p[0] - cx) * e1[0] + (p[1] - cy) * e1[1]) for p in points)
    b = max(abs(-(p[0] - cx) * e1[1] + (p[1] - cy) * e1[0]) for p in points)
    return a, b


def major_axis(points):
    n = len(points)
    cx, cy = sum(p[0] for p in points) / n, sum(p[1] for p in points) / n
    cxx = sum((p[0] - cx) ** 2 for p in points); cyy = sum((p[1] - cy) ** 2 for p in points); cxy = sum((p[0] - cx) * (p[1] - cy) for p in points)
    lam = (cxx + cyy) / 2 + math.sqrt(((cxx - cyy) / 2) ** 2 + cxy ** 2)
    e = (lam - cyy, cxy)
    if e[0] ** 2 + e[1] ** 2 < 1e-30:
        e = (cxy, lam - cxx)
    if e[0] ** 2 + e[1] ** 2 < 1e-30:
        return (1.0, 0.0), lam, (cxx + cyy) - lam
    k = math.hypot(*e)
    return (e[0] / k, e[1] / k), lam, (cxx + cyy) - lam


def bolt(centre, axis, up, half_len, half_w, cards=4, uv_scale=1.0, colour=0xff000000):
    """A bullet body of `cards` crossed cards along `axis` (6 vertices each,
    two triangles), the way the writer expands a body per instance; UVs differ
    per card so the body's period is its own vertex count."""
    ax = axis; k = math.sqrt(sum(a * a for a in ax)); ax = [a / k for a in ax]
    side = [ax[1] * up[2] - ax[2] * up[1], ax[2] * up[0] - ax[0] * up[2], ax[0] * up[1] - ax[1] * up[0]]
    k = math.sqrt(sum(a * a for a in side)); side = [a / k for a in side]
    up2 = [side[1] * ax[2] - side[2] * ax[1], side[2] * ax[0] - side[0] * ax[2], side[0] * ax[1] - side[1] * ax[0]]
    out = []
    for c in range(cards):
        ang = math.pi * c / cards
        n = [math.cos(ang) * side[i] + math.sin(ang) * up2[i] for i in range(3)]
        corners = [(-1, -1), (1, -1), (1, 1), (-1, -1), (1, 1), (-1, 1)]
        uvs = [(0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1)]
        for (s, t), (u, v) in zip(corners, uvs):
            p = [centre[i] + s * half_len * ax[i] + t * half_w * n[i] for i in range(3)]
            out.append((f32(p[0]), f32(p[1]), f32(p[2]), f32((u + c) * uv_scale), f32(v * uv_scale), colour))
    return out


class Harness:
    _dir = None
    _exe = None

    @classmethod
    def build(cls):
        if cls._exe:
            return cls._exe
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('no C++ compiler on the host')
        cls._dir = Path(tempfile.mkdtemp(prefix='bolt-footprint-'))
        (cls._dir / 'harness.cpp').write_text(HARNESS)
        exe = cls._dir / 'harness'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(cls._dir / 'harness.cpp'), '-o', str(exe)],
                               capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stderr)
        cls._exe = exe
        return exe

    @classmethod
    def run(cls, *args):
        exe = cls.build()
        run = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise AssertionError(f'harness {args} exit {run.returncode}: {run.stderr}')
        return run.stdout

    @classmethod
    def draw(cls, rows, viewport, r_px, g_px, vertices):
        cls.build()
        path = cls._dir / f'draw-{hashlib.sha1(repr((rows, viewport, r_px, g_px, vertices)).encode()).hexdigest()[:12]}.txt'
        lines = [' '.join(repr(v) for v in rows), ' '.join(str(v) for v in viewport), f'{r_px!r} {g_px!r}', str(len(vertices))]
        lines += [f'{x!r} {y!r} {z!r} {u!r} {v!r} {c}' for x, y, z, u, v, c in vertices]
        path.write_text('\n'.join(lines) + '\n')
        return parse_draw(cls.run('draw', str(path)))


def parse_draw(text):
    out = {'plans': [], 'out': []}
    for line in text.splitlines():
        parts = line.split()
        if parts[0] == 'FRAME':
            out['frame'] = parts[1]
        elif parts[0] == 'STATS':
            out['stats'] = {k: int(v) for k, v in (p.split('=') for p in parts[1:])}
        elif parts[0] == 'PLAN':
            fields = dict(p.split('=') for p in parts[2:])
            out['plans'].append({'verdict': int(fields['verdict']), 'a': float.fromhex(fields['a']), 'b': float.fromhex(fields['b']),
                                 's1': float.fromhex(fields['s1']), 's2': float.fromhex(fields['s2']),
                                 'qc': tuple(float.fromhex(v) for v in fields['qc'].split(',')), 'e1': tuple(float.fromhex(v) for v in fields['e1'].split(','))})
        elif parts[0] == 'WRITTEN':
            out['written'] = int(parts[1])
        elif parts[0] == 'OUT':
            out['out'].append(((float.fromhex(parts[2]), float.fromhex(parts[3]), float.fromhex(parts[4])), (parts[5], parts[6], parts[7]), int(parts[8])))
    return out


UNTOUCHED, EXPANDED, REFUSED_W, NONFINITE = 0, 1, 2, 3


class Scene:
    """A camera frame at X3 sector coordinates (|p| ~ 1e5) so the float32
    rounding of the written positions is the one the game sees."""
    def __init__(self, seed=1, width=1920, height=1080, origin=(-112000.0, 3000.0, 45000.0)):
        rng = random.Random(seed)
        self.camera = [origin[i] + rng.uniform(-50, 50) for i in range(3)]
        self.rot = rotation(rng.uniform(-math.pi, math.pi), rng.uniform(-0.6, 0.6), rng.uniform(-0.5, 0.5))
        self.forward = [self.rot[2][i] for i in range(3)]
        self.right = [self.rot[0][i] for i in range(3)]
        self.up = [self.rot[1][i] for i in range(3)]
        self.viewport = (0, 0, width, height)
        m = mat4(perspective(math.radians(60), width / height, 6.0, 1e6), view_matrix(self.camera, self.rot))
        self.rows = rows_of(m)
        self.px_per_rad = (height / 2) / math.tan(math.radians(30))

    def place(self, depth, lateral=(0.0, 0.0)):
        return [self.camera[i] + depth * self.forward[i] + lateral[0] * self.right[i] + lateral[1] * self.up[i] for i in range(3)]


class CoreRules(unittest.TestCase):
    @staticmethod
    def fp32_tolerance(rows, vp, points):
        """The fp32 bound of the harness's projection against the double oracle,
        per instance: the dp4 rounding of the x/y rows (2^-22 x the largest
        |term| sum, cancelling translation included) plus the world quantum of
        the written positions (the ulp of the coordinate magnitude, 0.0078 m at
        1e5 and 0.0625 m at 8e5, times the row scale), over the smallest w,
        in pixels; the GPU's own dp4 makes the same error. About 0.5 px at
        w = 200 m and 0.05 px at 2 km for the 8e5 scene."""
        terms = max(sum(abs(rows[4 * r + k] * p[k]) for k in range(3)) + abs(rows[4 * r + 3]) for p in points for r in (0, 1))
        w_min = min(clip(rows, p)[3] for p in points)
        magnitude = max(abs(c) for p in points for c in p)
        quantum = 2.0 ** (math.floor(math.log2(magnitude)) - 23)
        row_scale = max(math.sqrt(sum(rows[4 * r + k] ** 2 for k in range(3))) for r in (0, 1))
        return 0.05 + (2.0 ** -22 * terms + quantum * row_scale) / w_min * max(vp[2], vp[3]) / 2

    def check_instances(self, scene, result, vertices, r_px, g_px):
        """Every instance against the rule and the oracle projection, within
        the derived fp32 tolerance of its own depth and coordinates."""
        period = result['stats']['period']
        self.assertGreater(period, 0)
        rows, vp = scene.rows, scene.viewport
        self.observed = []  # (|oracle - plan| px, w_min m) per projected instance: the measured fp32 residue
        for index, plan in enumerate(result['plans']):
            src = vertices[index * period:(index + 1) * period]
            dst = result['out'][index * period:(index + 1) * period]
            src_pts = [(x, y, z) for x, y, z, _, _, _ in src]
            behind = any(clip(rows, p)[3] <= W_EPSILON for p in src_pts)
            same = all(s for _, _, s in dst)
            for (x, y, z, u, v, c), (_, words, _) in zip(src, dst):
                self.assertEqual(words, (f'{struct.unpack("<I", struct.pack("<f", u))[0]:08x}', f'{struct.unpack("<I", struct.pack("<f", v))[0]:08x}', f'{c:08x}'), 'UV/colour words verbatim')
            if behind:
                self.assertEqual(plan['verdict'], REFUSED_W, index)
                self.assertTrue(same, 'refused instance untouched byte for byte')
                continue
            before = [pixel(rows, vp, p)[0] for p in src_pts]
            tol = self.fp32_tolerance(rows, vp, src_pts)
            e1 = plan['e1']
            self.assertAlmostEqual(math.hypot(*e1), 1.0, places=5)
            a0, b0 = extents(before, e1)
            self.observed.append((max(abs(a0 - plan['a']), abs(b0 - plan['b'])), min(clip(rows, p)[3] for p in src_pts)))
            self.assertAlmostEqual(a0, plan['a'], delta=tol, msg=f'instance {index} A before')
            self.assertAlmostEqual(b0, plan['b'], delta=tol, msg=f'instance {index} B before')
            # The oracle's own major axis agrees with the plan's when the axes are distinct.
            oracle_e1, lam1, lam2 = major_axis(before)
            if lam1 > 4 * lam2 + 1e-6:
                self.assertGreater(abs(oracle_e1[0] * e1[0] + oracle_e1[1] * e1[1]), 0.999, index)
            # The rule from the plan's own A/B (equal to the oracle's within tol above; near the
            # 0.05 px floor target/B is hypersensitive to that fp32 residue).
            pa, pb = plan['a'], plan['b']
            t = min(1.0, max(0.0, (pa - r_px) / (g_px - r_px)))
            target_b = r_px * (1 - t)
            expect_s1 = max(1.0, r_px / max(pa, EXTENT_FLOOR)); expect_s2 = max(1.0, target_b / max(pb, EXTENT_FLOOR))
            self.assertAlmostEqual(plan['s1'], expect_s1, delta=0.05 * expect_s1 + 1e-3, msg=f'instance {index} s1')
            self.assertAlmostEqual(plan['s2'], expect_s2, delta=0.05 * expect_s2 + 1e-3, msg=f'instance {index} s2')
            if expect_s1 == 1.0 and expect_s2 == 1.0:
                self.assertEqual(plan['verdict'], UNTOUCHED, index)
                self.assertTrue(same, f'instance {index} untouched byte for byte')
                continue
            self.assertEqual(plan['verdict'], EXPANDED, index)
            self.assertFalse(same, index)
            after_pts = [p for p, _, _ in dst]
            after = [pixel(rows, vp, p)[0] for p in after_pts]
            a1, b1 = extents(after, e1)
            # The targets, bounded by the scale floor R / extent_floor for a sub-0.05 px extent.
            self.assertGreaterEqual(a1, pa * expect_s1 - tol, f'instance {index} A after ({pa:.3f} -> {a1:.3f}, s1 {expect_s1:.2f})')
            self.assertGreaterEqual(b1, pb * expect_s2 - tol, f'instance {index} B after ({pb:.3f} -> {b1:.3f}, s2 {expect_s2:.2f}, target {target_b:.3f})')
            if pa >= EXTENT_FLOOR and pa < r_px:
                self.assertGreaterEqual(a1, r_px - tol, f'instance {index} reaches R')
            if pb >= EXTENT_FLOOR and pb < target_b:
                self.assertGreaterEqual(b1, target_b - tol, f'instance {index} reaches R(1-t)')
            # Centroid kept, depth kept: clip w and z per vertex.
            self.assertAlmostEqual(sum(p[0] for p in after) / period, plan['qc'][0], delta=0.1)
            self.assertAlmostEqual(sum(p[1] for p in after) / period, plan['qc'][1], delta=0.1)
            # Absolute: clip w is the view depth in metres and clip z is affine in it; the
            # residue is the fp32 rounding of p + delta, one world quantum per component
            # (0.0078 m at |p| ~ 1e5, 0.0625 m at 8e5), so the bound is 0.05 m or two quanta.
            magnitude = max(abs(c) for p in src_pts for c in p)
            depth_tol = max(0.05, 2.0 * 2.0 ** (math.floor(math.log2(magnitude)) - 23))
            for p0, p1 in zip(src_pts, after_pts):
                c0, c1 = clip(rows, p0), clip(rows, p1)
                self.assertLessEqual(abs(c1[3] - c0[3]), depth_tol, f'instance {index} clip w')
                self.assertLessEqual(abs(c1[2] - c0[2]), depth_tol, f'instance {index} clip z')

    def test_parameters(self):
        self.assertEqual(Harness.run('params').split()[1:], ['1', '0', '0', '1', '0', '0', '0'])

    def test_period_rule(self):
        found = {}
        for line in Harness.run('period').splitlines():
            _, name, value = line.split()[:3]
            found[name] = int(value)
        self.assertEqual(found['point_sing_x2'], 24)
        self.assertEqual(found['energy_bolt_x3'], 84)
        self.assertEqual(found['repeat_x2'], 234)
        self.assertEqual(found['single_84'], 84)
        self.assertEqual(found['third_person_33x72'], 72)
        self.assertEqual(found['remapped_x3'], 0, 'per-instance UV remap: fail closed')
        self.assertEqual(found['not_multiple_of_3'], 0)
        self.assertEqual(found['too_short_21'], 0)
        self.assertEqual(found['past_max_period_300'], 0, 'a body past max_period: fail closed')
        self.assertEqual(found['truncated_count'], 0, 'a count no candidate divides: fail closed')
        self.assertEqual(found['single_255'], 0, 'past the largest stock body')
        self.assertEqual(found['single_234'], 234)
        self.assertEqual(found['remapped_x2_168'], 168, 'two remapped bodies within max_period are one instance (documented)')
        self.assertEqual(found['symmetric_84_sub42'], 84, 'a non-stock sub-period (42) inside a stock body (84) that divides the count: promoted to the body')
        self.assertEqual(found['repeat_x2_sub78'], 234, 'bullet_Repeat: 78 is no stock length, promoted to the body')
        self.assertEqual(found['repeat_x1_sub78'], 234)
        self.assertEqual(found['mod_78_x2'], 78, 'two 78-vertex bodies (234 does not divide 156): kept')
        self.assertEqual(found['plasma_x3_sub24'], 24, 'PlasmaBeam/Repair thirds are stock-length groups sharing the centroid: split, harmless')
        self.assertEqual(found['null'], 0)

    def test_scan_keeps_the_other_words(self):
        line = Harness.run('scan').strip()
        self.assertEqual(line, 'SCAN scanned=40 published=40 ok=1')

    def test_regimes_below_between_above_and_behind(self):
        scene = Scene(seed=3)
        px = scene.px_per_rad
        vertices = []
        # 1: end-on far bolt, cross-section ~0.3 px: below R.
        vertices += bolt(scene.place(3000), scene.forward, scene.up, 2.0, 0.5, uv_scale=1.0)
        # 2: side-on bolt, half-length ~4.5 px: between R and G (s1 = 1, B raised to R(1-t)).
        vertices += bolt(scene.place(2000, (30, 10)), scene.right, scene.up, 4.5 * 2000 / px, 0.5, uv_scale=1.0)
        # 3: side-on near bolt, half-length ~16 px >= G: untouched byte for byte.
        vertices += bolt(scene.place(600, (-20, 5)), scene.right, scene.up, 16 * 600 / px, 0.5, uv_scale=1.0)
        # 4: behind the camera plane: refused, untouched.
        vertices += bolt(scene.place(-50, (3, 3)), scene.right, scene.up, 3.0, 0.5, uv_scale=1.0)
        # 5: straddling the camera plane: refused, untouched.
        vertices += bolt(scene.place(0.5, (0, 0)), scene.forward, scene.up, 3.0, 0.5, uv_scale=1.0)
        result = Harness.draw(scene.rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)
        self.assertEqual(result['frame'], 'ok')
        s = result['stats']
        self.assertEqual((s['ok'], s['period'], s['instances']), (1, 24, 5))
        self.assertEqual((s['expanded'], s['untouched'], s['refused_w']), (2, 1, 2))
        self.assertEqual(result['written'], 2)
        verdicts = [p['verdict'] for p in result['plans']]
        self.assertEqual(verdicts, [EXPANDED, EXPANDED, UNTOUCHED, REFUSED_W, REFUSED_W])
        self.assertLess(result['plans'][0]['a'], R_DEFAULT)
        self.assertTrue(R_DEFAULT <= result['plans'][1]['a'] < G_DEFAULT)
        self.assertEqual(result['plans'][1]['s1'], 1.0)
        self.assertGreaterEqual(result['plans'][2]['a'], G_DEFAULT)
        self.check_instances(scene, result, vertices, R_DEFAULT, G_DEFAULT)

    def test_continuity_across_the_gate(self):
        # Half-length stepping through G: the minor target R(1-t) falls to zero
        # continuously and the instance at G and beyond is untouched.
        scene = Scene(seed=5)
        px = scene.px_per_rad
        previous = None
        for half_px in (3.0, 4.0, 5.5, 7.0, 7.9, 8.0, 8.5, 12.0, 20.0):
            vertices = bolt(scene.place(1500, (5, -5)), scene.right, scene.up, half_px * 1500 / px, 0.25, uv_scale=1.0)
            result = Harness.draw(scene.rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)
            plan = result['plans'][0]
            self.check_instances(scene, result, vertices, R_DEFAULT, G_DEFAULT)
            if plan['a'] >= G_DEFAULT:
                self.assertEqual(plan['verdict'], UNTOUCHED, half_px)
                self.assertTrue(all(s for _, _, s in result['out']))
                continue
            target_b = R_DEFAULT * (1 - (plan['a'] - R_DEFAULT) / (G_DEFAULT - R_DEFAULT))
            if previous is not None:
                self.assertLessEqual(target_b, previous + 1e-6, 'the minor target decreases towards the gate')
            previous = target_b
            # Below the gate the instance is written only while its minor extent is under the fading target.
            self.assertEqual(plan['verdict'], EXPANDED if plan['b'] < target_b else UNTOUCHED, half_px)
        self.assertLess(previous, 0.1, 'the minor target is nearly zero just under the gate')

    def test_bolt_of_2g_or_more_untouched_byte_for_byte(self):
        scene = Scene(seed=8)
        px = scene.px_per_rad
        vertices = []
        for k in range(6):
            vertices += bolt(scene.place(200 + 40 * k, (2 * k, -k)), scene.right, scene.up, 12.0, 0.4, uv_scale=1.0)
        result = Harness.draw(scene.rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)
        self.assertEqual(result['stats']['expanded'], 0)
        self.assertEqual(result['stats']['untouched'], 6)
        self.assertEqual(result['written'], 0)
        self.assertTrue(all(s for _, _, s in result['out']), 'first-person bolts: every byte the game\'s')
        for p in result['plans']:
            self.assertGreaterEqual(p['a'], G_DEFAULT)

    def test_random_instances(self):
        rng = random.Random(2026)
        seeds = (11, 12, 13)
        for seed in seeds:
            scene = Scene(seed=seed)
            px = scene.px_per_rad
            vertices = []
            for k in range(40):
                depth = rng.choice((rng.uniform(80, 400), rng.uniform(400, 3000), rng.uniform(3000, 30000)))
                axis = rng.choice((scene.forward, scene.right, [rng.uniform(-1, 1) for _ in range(3)]))
                half_len = rng.uniform(0.5, 12.0); half_w = rng.uniform(0.1, 1.0)
                lateral = (rng.uniform(-0.4, 0.4) * depth, rng.uniform(-0.25, 0.25) * depth)
                vertices += bolt(scene.place(depth, lateral), axis, scene.up, half_len, half_w, uv_scale=1.0)
            r_px, g_px = rng.choice(((3.0, 8.0), (2.0, 6.0), (4.0, 12.0)))
            result = Harness.draw(scene.rows, scene.viewport, r_px, g_px, vertices)
            self.assertEqual(result['frame'], 'ok')
            self.assertEqual(result['stats']['instances'], 40)
            self.assertGreater(result['stats']['expanded'], 0)
            self.assertGreater(result['stats']['untouched'], 0)
            self.check_instances(scene, result, vertices, r_px, g_px)
            self.assertEqual(result['written'], result['stats']['expanded'])

    def test_far_sector_coordinates(self):
        # |p| ~ 8e5 (world quantum 0.0625 m): the derived tolerance is ~0.5 px
        # at w = 200 m and ~0.05 px at 2 km; the rule holds within it and the
        # depth invariance holds within two quanta (0.125 m).
        rng = random.Random(77)
        scene = Scene(seed=21, origin=(790000.0, -310000.0, 560000.0))
        vertices = []
        for k in range(30):
            depth = rng.choice((rng.uniform(150, 400), rng.uniform(400, 3000), rng.uniform(3000, 20000)))
            axis = rng.choice((scene.forward, scene.right, [rng.uniform(-1, 1) for _ in range(3)]))
            vertices += bolt(scene.place(depth, (rng.uniform(-0.3, 0.3) * depth, rng.uniform(-0.2, 0.2) * depth)), axis, scene.up, rng.uniform(0.5, 10.0), rng.uniform(0.1, 1.0), uv_scale=1.0)
        result = Harness.draw(scene.rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)
        self.assertEqual(result['frame'], 'ok')
        self.assertEqual(result['stats']['instances'], 30)
        self.assertGreater(result['stats']['expanded'], 0)
        self.check_instances(scene, result, vertices, R_DEFAULT, G_DEFAULT)
        # The measured fp32 residue stays under the world-quantum figure (0.5 px
        # at 200 m, 0.05 px at 2 km, scaling with 1/w) plus 0.05 px; the derived
        # tolerance above it also carries the dp4 term (up to ~1.4 px at 350 m).
        self.assertEqual(len(self.observed), 30)
        for deviation, w in self.observed:
            self.assertLessEqual(deviation, 0.05 + 0.5 * 200 / w, f'observed {deviation:.3f} px at w = {w:.0f} m')

    def test_rows_that_are_not_a_perspective_projection_are_refused(self):
        scene = Scene(seed=2)
        vertices = bolt(scene.place(2000), scene.forward, scene.up, 2.0, 0.5)
        rows = list(scene.rows)
        rows[8] += 0.5 * rows[13]; rows[9] -= 0.5 * rows[12]  # the z row no longer parallel to the w row
        result = Harness.draw(rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)
        self.assertEqual(result['frame'], 'not_perspective')
        self.assertEqual(result['stats']['ok'], 0)
        rows = list(scene.rows); rows[12] = rows[13] = rows[14] = 0.0
        self.assertEqual(Harness.draw(rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)['frame'], 'degenerate')
        rows = list(scene.rows); rows[5] = float('nan')
        self.assertEqual(Harness.draw(rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)['frame'], 'nonfinite')
        self.assertEqual(Harness.draw(scene.rows, (0, 0, 0, 1080), R_DEFAULT, G_DEFAULT, vertices)['frame'], 'viewport')

    def test_remapped_stream_leaves_the_draw_untouched(self):
        scene = Scene(seed=4)
        vertices = []
        for k in range(3):
            vertices += bolt(scene.place(3000, (k, 0)), scene.forward, scene.up, 2.0, 0.5, cards=14, uv_scale=1.0 + 0.01 * k)
        result = Harness.draw(scene.rows, scene.viewport, R_DEFAULT, G_DEFAULT, vertices)
        self.assertEqual(result['stats']['ok'], 0)
        self.assertEqual(result['stats']['period'], 0)
        self.assertNotIn('written', result)


class Wiring(unittest.TestCase):
    def test_proxy_route(self):
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        additive = motion[motion.index('void MotionOutput::prepare_screen_additive('):motion.index('void MotionOutput::apply_screen_additive_alpha') if 'void MotionOutput::apply_screen_additive_alpha' in motion else None]
        self.assertIn('++screen_additive_admitted_; ++screen_additive_frame_admitted_;', additive)
        self.assertIn('if (bolt_footprint_requested_) prepare_bolt_footprint(call, route);', additive)
        self.assertLess(additive.index('++screen_additive_admitted_'), additive.index('prepare_bolt_footprint(call, route)'), 'after the admission, never before a refusal')
        finish = motion[motion.index('void MotionOutput::finish_screen_additive('):]
        self.assertTrue(finish.startswith('void MotionOutput::finish_screen_additive(MotionRoute& route) noexcept {\n    if (route.bolt_footprint) finish_bolt_footprint(route);'))
        prepare = motion[motion.index('void MotionOutput::prepare_bolt_footprint('):motion.index('void MotionOutput::finish_bolt_footprint(')]
        # The bullet producers only, before any counter: the same guard step D's derive_prefix_region opens with.
        gate = 'if (!screen_emission::admitted_vertex_shader(shadow_.vs_hash)) return;'
        self.assertIn(gate, prepare)
        self.assertLess(prepare.index(gate), prepare.index('++c.draws;'))
        step_d = motion[motion.index('void MotionOutput::derive_prefix_region('):]
        self.assertLess(step_d.index(gate), step_d.index('++counts.prefix_draws;'))
        for needle in ('fade_region::locked_prefix_vertices(query, count, &positions, &extras, &revision, &refusal)',
                       'fade_region::recheck_locked_prefix(query, count, revision)', 'D3DLOCK_DISCARD',
                       'GetStreamSourceFreq, 0, &frequency)', 'reinterpret_cast<std::uintptr_t>(stream) != shadow_.stream0_identity',
                       'native<SetStreamFn>(SetStreamSource)(device_, 0, bolt_vb_, 0, stride)', 'if (!stats.expanded) { ++c.untouched; return; }'):
            self.assertIn(needle, prepare)
        self.assertLess(prepare.index('bolt_vb_->Unlock()'), prepare.index('recheck_locked_prefix'), 'the copy is rechecked after the unlock, before the binding')
        self.assertLess(prepare.index('recheck_locked_prefix'), prepare.index('SetStreamSource)(device_, 0, bolt_vb_'))
        self.assertIn('const bool timed = telemetry_ || bolt_window_frames_ + 1u >= 300u;', prepare, 'QPC only with telemetry or on the window frame')
        self.assertIn('BoltTiming timing{timed, c.ticks};', prepare)
        ensure = motion[motion.index('bool MotionOutput::ensure_bolt_buffer('):motion.index('void MotionOutput::release_bolt_buffer(')]
        self.assertIn('D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT', ensure)
        self.assertIn('const UINT size = UINT(fade_region::prefix::max_bytes);', ensure)
        self.assertIn('if (bolt_vb_) return bolt_vb_bytes_ >= bytes;', ensure, 'created once, never regrown on the render thread')
        reset = motion[motion.index('void MotionOutput::before_reset()'):motion.index('void MotionOutput::after_reset(')]
        self.assertIn('release_bolt_buffer();', reset)
        release = motion[:motion.index('void MotionOutput::before_reset()')]
        self.assertIn('release_fade_witness(); release_packed_sample(); release_bolt_buffer();', release)
        self.assertIn('if (bolt_footprint_requested_ && ++bolt_window_frames_ >= 300u) log_bolt_footprint_window();', motion)
        self.assertIn('CreateVertexBuffer = 26', motion)
        self.assertIn('SLOT(IDirect3DDevice9Vtbl, CreateVertexBuffer, 26);', (ROOT / 'verification/probe/abi_check.cpp').read_text())

    def test_dll_gate_and_loader(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = capture[capture.index('X3M_BOLT_FOOTPRINT",setting'):capture.index('bolt_footprint_mode requested=1')]
        self.assertIn('bolt_footprint_requested=valid&&screen_emission_additive_requested&&ownership;', block)
        self.assertIn('r>0.f&&r<=64.f&&g>r&&g<=256.f', block)
        self.assertIn('if(length&&!(parsed&&r==0.f)){', block, 'an over-long value (31+ chars, unparsed) logs the refused mode line')
        self.assertIn('const bool fits=length&&length<32;', block)
        self.assertLess(capture.index('screen_emission_additive_mode requested=1'), capture.index('X3M_BOLT_FOOTPRINT",setting'), 'parsed after the additive gate it needs')
        self.assertIn('hooked.motion_output.configure_bolt_footprint(bolt_footprint_requested,bolt_footprint_r,bolt_footprint_g);', capture)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        self.assertIn('const bool prefix_requested = bound_requested || footprint_requested;', loader)
        self.assertIn('"bolt_footprint_only"', loader)
        core = (ROOT / 'src/proxy/locked_prefix_core.h').read_text()
        self.assertIn('if (extras) std::memcpy(extras + std::size_t(i) * 3, words + 3, 12);', core)
        header = re.sub(r'//[^\n]*', '', CORE.read_text())
        self.assertNotIn('double', header, 'single-precision only on the draw path')
        self.assertNotIn('std::sqrt', header); self.assertNotIn('std::fabs', header); self.assertNotIn('std::abs', header)
        self.assertNotIn('windows.h', header); self.assertNotIn('d3d9.h', header)


def load_manage():
    return load('bolt_footprint_manage', ROOT / 'tools/manage.py')


class LauncherOption(unittest.TestCase):
    PREREQUISITES = ['--motion-output', '--hdr', '--ownership']

    def launch(self, directory, *args, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, {'X3M_BOLT_FOOTPRINT': '9,99'}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, **kw):
        code, output, error = self.launch(directory, *args, **kw)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_default_on_a_modded_launch_without_implying_the_additive_route(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory)
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '3,8')
            self.assertEqual(env['X3M_SCREEN_EMISSION_ADDITIVE'], '0')
            env = self.env(directory, *self.PREREQUISITES)
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('3,8', '0'))
            env = self.env(directory, *self.PREREQUISITES, '--screen-emission-additive', '2')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('3,8', '2.0'))

    def test_opt_out_and_custom_values(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.env(directory, *self.PREREQUISITES)
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '0')
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '0')
            self.assertEqual({k: v for k, v in env.items() if k != 'X3M_BOLT_FOOTPRINT'}, {k: v for k, v in baseline.items() if k != 'X3M_BOLT_FOOTPRINT'})
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '4,10')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('4,10', '1.0'), 'an explicit value implies the additive route at gain 1')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '4')
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '4,8')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '2.5,6.5', '--screen-emission-additive', '2')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('2.5,6.5', '2.0'), 'a given gain is kept')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('3,8', '1.0'))

    def test_vanilla_forwards_nothing_and_refuses_an_explicit_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_BOLT_FOOTPRINT', self.env(directory, vanilla=True))
            self.assertNotIn('X3M_BOLT_FOOTPRINT', self.env(directory, '--bolt-footprint', '0', vanilla=True))
            for args in (('--bolt-footprint',), ('--bolt-footprint', '3,8')):
                code, _, error = self.launch(directory, *args, vanilla=True)
                self.assertEqual(code, 2, args)
                self.assertIn('--bolt-footprint cannot be combined with --vanilla', error)

    def test_explicit_value_needs_the_additive_prerequisites(self):
        with tempfile.TemporaryDirectory() as directory:
            for missing in self.PREREQUISITES:
                code, _, error = self.launch(directory, *[a for a in self.PREREQUISITES if a != missing], '--bolt-footprint', '3,8')
                self.assertEqual(code, 2, missing)
                if missing != '--motion-output':  # --hdr's own prerequisite error comes first without it
                    self.assertIn('--bolt-footprint requires --motion-output --hdr --ownership', error)
            code, _, error = self.launch(directory, '--taa', '--object-trace', '--object-lifetime', '--hdr-tonemap', '--hdr-decode', 'gamma2.2', *self.PREREQUISITES, '--screen-emission', '--bolt-footprint', '3,8')
            self.assertEqual(code, 2)
            self.assertIn('--bolt-footprint needs the additive bullets', error)
            self.assertEqual(self.env(directory, '--bolt-footprint', '0')['X3M_BOLT_FOOTPRINT'], '0', 'the off value needs nothing')

    def test_malformed_values_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for bad in ('2,1', 'x', '0,5', '70', '3,300', '1,2,3', '3,', ',8', 'nan', 'inf,9', '-1'):
                code, _, error = self.launch(directory, *self.PREREQUISITES, '--bolt-footprint', bad)
                self.assertEqual(code, 2, bad)
                self.assertIn('--bolt-footprint expects R or R,G', error)

    def test_help_names_the_default_and_the_gates(self):
        source = (ROOT / 'tools/manage.py').read_text()
        self.assertIn("parser.add_argument('--bolt-footprint', nargs='?', const=BOLT_FOOTPRINT_DEFAULT, default=None, metavar='R[,G]'", source)
        self.assertIn("BOLT_FOOTPRINT_DEFAULT = '3,8'", source)
        for phrase in ('launcher default on modded launches: 3,8', '--bolt-footprint 0 = off', 'X3M_BOLT_FOOTPRINT=R[,G]', 'docs/architecture/bolt-footprint.md'):
            self.assertIn(phrase, source)


if __name__ == '__main__':
    unittest.main()
