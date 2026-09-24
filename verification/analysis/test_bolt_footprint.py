"""Host checks of the bolt footprint (src/proxy/bolt_footprint_core.h,
docs/architecture/bolt-footprint.md option A', Run 73 B visibility rule).

The core compiled from its header on the host: the instance period rule on
synthetic UV streams (found for 24/84/234-vertex bodies, refused for a
remapped stream, a count that is no multiple of 3, a body past max_period), the
widening of instances narrower than W_min and the lengthening of instances
shorter than L_min along their projected world axis against an independent
double-precision projection of the written vertices (extents reach their
targets, an end-on bolt becomes an L x W streak pointing at its vanishing
point, continuity at both minimums, clip z and w unchanged, an instance behind
the camera plane refused, bolts at or above both minimums - the first-person
sizes - untouched byte for byte), the size histogram, the rows test, the
widened Unlock scan (locked_prefix_core.h keeps the UV/colour words), the proxy
wiring with the chase-view gate and the --bolt-footprint launcher gates
(--dry-run only, never a launch). No Wine.
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
W_DEFAULT, L_DEFAULT = 3.0, 12.0
W_EPSILON = 1e-3
EXTENT_FLOOR = 0.05
MIN_MOVE_PX = 0.25
HIST_EDGES = (0.5, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 32)

HARNESS = r'''
#include "bolt_footprint_core.h"
#include "locked_prefix_core.h"
#include "chase_camera.h"
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
    float rows[16]; unsigned vx, vy, vw, vh; float w_px, l_px; unsigned count;
    for (float& v : rows) if (std::fscanf(f, "%f", &v) != 1) return 3;
    if (std::fscanf(f, "%u %u %u %u", &vx, &vy, &vw, &vh) != 4) return 3;
    if (std::fscanf(f, "%f %f", &w_px, &l_px) != 2) return 3;
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
    Histogram hist;
    const bool planned = plan_draw(frame, positions.data(), extras.data(), count, w_px, l_px, plans.data(), max_instances, &stats, &hist);
    std::printf("STATS ok=%u period=%u instances=%u expanded=%u untouched=%u refused_w=%u nonfinite=%u lengthened=%u widened=%u world_axis=%u disc=%u\n", unsigned(planned), stats.period, stats.instances, stats.expanded, stats.untouched, stats.refused_w, stats.nonfinite, stats.lengthened, stats.widened, stats.world_axis, stats.disc);
    if (!planned) return 0;
    Histogram box;
    const bool boxed = histogram_draw(frame, positions.data(), extras.data(), count, &box);
    std::printf("BBOX %u %u", unsigned(boxed), unsigned(box.instances));
    for (unsigned i = 0; i < hist_buckets; ++i) std::printf(" %u", unsigned(box.half_length[i]));
    for (unsigned i = 0; i < hist_buckets; ++i) std::printf(" %u", unsigned(box.width[i]));
    std::printf("\n");
    std::printf("HIST %u", unsigned(hist.instances));
    for (unsigned i = 0; i < hist_buckets; ++i) std::printf(" %u", unsigned(hist.half_length[i]));
    for (unsigned i = 0; i < hist_buckets; ++i) std::printf(" %u", unsigned(hist.width[i]));
    std::printf("\n");
    for (unsigned i = 0; i < stats.instances; ++i) {
        const Plan& p = plans[i];
        std::printf("PLAN %u verdict=%u a=%a b=%a s1=%a s2=%a qc=%a,%a e1=%a,%a world_axis=%u disc=%u\n", i, unsigned(p.verdict), p.a, p.b, p.s1, p.s2, p.qc[0], p.qc[1], p.e1[0], p.e1[1], unsigned(p.world_axis), unsigned(p.disc));
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
    if (!std::strcmp(argv[1], "gate")) {
        // The frame-stamped view gate over a scripted frame sequence: the handler
        // publishes (written or not) at most once per frame before the draws, the
        // consumer takes its mark at Present. Per frame: the gate at the draws.
        struct Frame { int visit; bool installed; }; // visit: -1 none, 0 not written, 1 written
        const Frame frames[] = {{1, true}, {-1, true}, {1, true}, {0, true}, {-1, true}, {1, true}, {1, true}, {1, false}, {-1, true}};
        std::uint32_t writes = 0, mark = 0; bool last = false;
        std::printf("GATE");
        for (const Frame& fr : frames) {
            if (fr.visit >= 0) { last = fr.visit == 1; if (last) ++writes; }
            std::printf(" %u", unsigned(x3m::chase_camera::pose_gate_open(fr.installed, last, writes, mark)));
            mark = writes; // Present
        }
        std::printf("\n");
        return 0;
    }
    if (!std::strcmp(argv[1], "params")) {
        std::printf("PARAMS %u %u %u %u %u %u %u %u\n", unsigned(valid_parameters(3.f, 12.f)), unsigned(valid_parameters(0.f, 12.f)), unsigned(valid_parameters(8.f, 8.f)),
                    unsigned(valid_parameters(64.f, 256.f)), unsigned(valid_parameters(65.f, 256.f)), unsigned(valid_parameters(3.f, 257.f)), unsigned(valid_parameters(3.f, 0.f / 0.f)),
                    unsigned(valid_parameters(4.f, 3.f)));
        std::printf("BUCKETS");
        for (float v : {0.f, 0.49f, 0.5f, 0.99f, 1.f, 2.99f, 3.f, 11.9f, 12.f, 31.9f, 32.f, 1e6f}) std::printf(" %u", hist_bucket(v));
        std::printf("\n");
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


def axis_direction(rows, vp, centre, axis):
    """Screen direction (unit) of a world axis through `centre`: the derivative
    of the pixel projection along it, in double precision."""
    c, d = clip(rows, centre), [rows[4 * r] * axis[0] + rows[4 * r + 1] * axis[1] + rows[4 * r + 2] * axis[2] for r in range(4)]
    gx = vp[2] / 2 * (d[0] * c[3] - c[0] * d[3]); gy = -vp[3] / 2 * (d[1] * c[3] - c[1] * d[3])
    k = math.hypot(gx, gy)
    return (gx / k, gy / k) if k > 0 else None


def surface_axis(points):
    """The core's axis definition in double precision: the major eigenvector
    of the area-weighted second moment of the triangles (points: a triangle
    list), or None when the body is not elongated (lambda1 < 2 (l2 + l3))."""
    o = points[0]
    area = 0.0; m1 = [0.0] * 3; m2 = [[0.0] * 3 for _ in range(3)]
    for t in range(0, len(points) - 2, 3):
        v = [[points[t + k][i] - o[i] for i in range(3)] for k in range(3)]
        e0 = [v[1][i] - v[0][i] for i in range(3)]; e2 = [v[2][i] - v[0][i] for i in range(3)]
        n = [e0[1] * e2[2] - e0[2] * e2[1], e0[2] * e2[0] - e0[0] * e2[2], e0[0] * e2[1] - e0[1] * e2[0]]
        a = 0.5 * math.sqrt(sum(x * x for x in n))
        if a <= 0:
            continue
        sm = [v[0][i] + v[1][i] + v[2][i] for i in range(3)]
        area += a
        for i in range(3):
            m1[i] += a / 3 * sm[i]
            for j in range(3):
                m2[i][j] += a / 12 * (v[0][i] * v[0][j] + v[1][i] * v[1][j] + v[2][i] * v[2][j] + sm[i] * sm[j])
    if area <= 0:
        return None
    mu = [m / area for m in m1]
    c = [[m2[i][j] / area - mu[i] * mu[j] for j in range(3)] for i in range(3)]
    x = [1.0, 0.7, 0.3]
    for _ in range(200):
        y = [sum(c[i][j] * x[j] for j in range(3)) for i in range(3)]
        k = math.sqrt(sum(v * v for v in y))
        if k == 0:
            return None
        x = [v / k for v in y]
    lam = sum(x[i] * sum(c[i][j] * x[j] for j in range(3)) for i in range(3))
    trace = c[0][0] + c[1][1] + c[2][2]
    return x if lam >= 2 * (trace - lam) else None


def hist_bucket(px):
    return next((i for i, e in enumerate(HIST_EDGES) if px < e), len(HIST_EDGES))


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
    def draw(cls, rows, viewport, w_px, l_px, vertices):
        cls.build()
        path = cls._dir / f'draw-{hashlib.sha1(repr((rows, viewport, w_px, l_px, vertices)).encode()).hexdigest()[:12]}.txt'
        lines = [' '.join(repr(v) for v in rows), ' '.join(str(v) for v in viewport), f'{w_px!r} {l_px!r}', str(len(vertices))]
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
        elif parts[0] == 'HIST':
            values = [int(v) for v in parts[1:]]
            n = len(HIST_EDGES) + 1
            out['hist'] = {'instances': values[0], 'half_length': values[1:1 + n], 'width': values[1 + n:1 + 2 * n]}
        elif parts[0] == 'BBOX':
            values = [int(v) for v in parts[1:]]
            n = len(HIST_EDGES) + 1
            out['bbox'] = {'ok': values[0], 'instances': values[1], 'half_length': values[2:2 + n], 'width': values[2 + n:2 + 2 * n]}
        elif parts[0] == 'PLAN':
            fields = dict(p.split('=') for p in parts[2:])
            out['plans'].append({'verdict': int(fields['verdict']), 'a': float.fromhex(fields['a']), 'b': float.fromhex(fields['b']),
                                 's1': float.fromhex(fields['s1']), 's2': float.fromhex(fields['s2']),
                                 'qc': tuple(float.fromhex(v) for v in fields['qc'].split(',')), 'e1': tuple(float.fromhex(v) for v in fields['e1'].split(',')),
                                 'world_axis': int(fields['world_axis']), 'disc': int(fields['disc'])})
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

    def check_instances(self, scene, result, vertices, w_px, l_px, axes=None):
        """Every instance against the rule and the oracle projection, within
        the derived fp32 tolerance of its own depth and coordinates. axes: the
        world axis of each instance (or None) for the length-axis check."""
        period = result['stats']['period']
        self.assertGreater(period, 0)
        rows, vp = scene.rows, scene.viewport
        self.observed = []  # (|oracle - plan| px, w_min m) per projected instance: the measured fp32 residue
        hist_l, hist_w = [0] * (len(HIST_EDGES) + 1), [0] * (len(HIST_EDGES) + 1)
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
            hist_l[hist_bucket(plan['a'])] += 1; hist_w[hist_bucket(2 * plan['b'])] += 1
            # The length axis: the projected world axis when the plan used it.
            if axes is not None and axes[index] is not None and plan['world_axis']:
                centre = [sum(p[k] for p in src_pts) / period for k in range(3)]
                oracle = axis_direction(rows, vp, centre, axes[index])
                self.assertGreater(abs(oracle[0] * e1[0] + oracle[1] * e1[1]), 0.999, f'instance {index} length axis')
            # The rule from the plan's own extents (equal to the oracle's within tol above);
            # a disc (no usable axis) sits on the screen axes and is raised to W only.
            pa, pb = plan['a'], plan['b']
            target_l = w_px if plan['disc'] else l_px
            if plan['disc']:
                self.assertEqual(e1, (1.0, 0.0), f'instance {index} disc on the screen axes')
                self.assertFalse(plan['world_axis'])
            expect_s1 = target_l / max(2 * pa, 2 * EXTENT_FLOOR) if 2 * pa < target_l else 1.0
            expect_s2 = w_px / max(2 * pb, 2 * EXTENT_FLOOR) if 2 * pb < w_px else 1.0
            if (expect_s1 - 1) * pa < MIN_MOVE_PX: expect_s1 = 1.0
            if (expect_s2 - 1) * pb < MIN_MOVE_PX: expect_s2 = 1.0
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
            # The targets, bounded by the scale floor L / (2 extent_floor) for a sub-0.1 px extent.
            self.assertGreaterEqual(a1, pa * expect_s1 - tol, f'instance {index} length after ({2 * pa:.3f} -> {2 * a1:.3f}, s1 {expect_s1:.2f})')
            self.assertGreaterEqual(b1, pb * expect_s2 - tol, f'instance {index} width after ({2 * pb:.3f} -> {2 * b1:.3f}, s2 {expect_s2:.2f})')
            if pa >= EXTENT_FLOOR and expect_s1 > 1.0:
                self.assertGreaterEqual(2 * a1, target_l - 2 * tol, f'instance {index} reaches L')
                self.assertLessEqual(2 * a1, target_l + 2 * tol, f'instance {index} stops at L')
            if pb >= EXTENT_FLOOR and expect_s2 > 1.0:
                self.assertGreaterEqual(2 * b1, w_px - 2 * tol, f'instance {index} reaches W')
                self.assertLessEqual(2 * b1, w_px + 2 * tol, f'instance {index} stops at W')
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
        if 'hist' in result:
            projected = sum(1 for p in result['plans'] if p['verdict'] in (UNTOUCHED, EXPANDED))
            self.assertEqual(result['hist']['instances'], projected)
            # Bucket boundaries are exact float compares on the plan's own extents.
            self.assertEqual(result['hist']['half_length'], hist_l)
            self.assertEqual(result['hist']['width'], hist_w)
        if 'bbox' in result:
            # The gated views' cheap histogram: projected bounding box per instance.
            box_l, box_w = [0] * (len(HIST_EDGES) + 1), [0] * (len(HIST_EDGES) + 1)
            count = 0
            for index in range(result['stats']['instances']):
                pts = [(x, y, z) for x, y, z, _, _, _ in vertices[index * period:(index + 1) * period]]
                if any(clip(rows, q)[3] <= W_EPSILON for q in pts):
                    continue
                px_ = [pixel(rows, vp, q)[0] for q in pts]
                sx = max(q[0] for q in px_) - min(q[0] for q in px_); sy = max(q[1] for q in px_) - min(q[1] for q in px_)
                major, minor = max(sx, sy), min(sx, sy)
                tol = self.fp32_tolerance(rows, vp, pts)
                # only exact-bucket compares away from an edge
                if any(abs(0.5 * major - e) < tol or abs(minor - e) < tol for e in HIST_EDGES):
                    continue
                count += 1; box_l[hist_bucket(0.5 * major)] += 1; box_w[hist_bucket(minor)] += 1
            self.assertEqual(result['bbox']['ok'], 1)
            projected = sum(1 for p in result['plans'] if p['verdict'] != REFUSED_W)
            self.assertEqual(result['bbox']['instances'], projected)
            # Every instance clear of a bucket edge (by the fp32 tolerance) sits in its
            # oracle bucket; the ones near an edge may fall either side.
            for name, oracle in (('half_length', box_l), ('width', box_w)):
                self.assertEqual(sum(result['bbox'][name]), projected)
                for bucket, (got, want) in enumerate(zip(result['bbox'][name], oracle)):
                    self.assertGreaterEqual(got, want, f'bbox {name} bucket {bucket}')

    def test_parameters(self):
        lines = Harness.run('params').splitlines()
        self.assertEqual(lines[0].split()[1:], ['1', '0', '1', '1', '0', '0', '0', '0'])
        self.assertEqual(lines[1].split()[1:], ['0', '0', '1', '1', '2', '4', '5', '8', '9', '10', '11', '11'])

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

    def test_below_and_above_the_minimums_and_behind(self):
        scene = Scene(seed=3)
        px = scene.px_per_rad
        specs = [
            # 1: chase-view bolt seen almost end-on (axis 3 deg off the view axis, 120 m below
            #    the view line at 3 km): length ~1.2 px, width ~0.3 px -> both raised.
            (scene.place(3000, (0, -120)), [scene.forward[i] * math.cos(0.05) + scene.up[i] * math.sin(0.05) for i in range(3)], 2.0, 0.5),
            # 2: side-on bolt 9 px long, 0.4 px wide: lengthened to 12 and widened to 3.
            (scene.place(2000, (30, 10)), scene.right, 4.5 * 2000 / px, 0.2 * 2000 / px),
            # 3: side-on bolt 20 px long, 1 px wide: widened only.
            (scene.place(1500, (-20, 5)), scene.right, 10 * 1500 / px, 0.5 * 1500 / px),
            # 4: near bolt 32 px long, 4 px wide: at or above both minimums, untouched byte for byte.
            (scene.place(600, (-20, 5)), scene.right, 16 * 600 / px, 2 * 600 / px),
            # 5: behind the camera plane: refused, untouched.
            (scene.place(-50, (3, 3)), scene.right, 3.0, 0.5),
            # 6: straddling the camera plane: refused, untouched.
            (scene.place(0.5, (0, 0)), scene.forward, 3.0, 0.5),
        ]
        vertices = []
        for centre, axis, half_len, half_w in specs:
            vertices += bolt(centre, axis, scene.up, half_len, half_w, uv_scale=1.0)
        result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
        self.assertEqual(result['frame'], 'ok')
        s = result['stats']
        self.assertEqual((s['ok'], s['period'], s['instances']), (1, 24, 6))
        self.assertEqual((s['expanded'], s['untouched'], s['refused_w']), (3, 1, 2))
        self.assertEqual((s['lengthened'], s['widened']), (2, 3))
        self.assertEqual(result['written'], 3)
        verdicts = [p['verdict'] for p in result['plans']]
        self.assertEqual(verdicts, [EXPANDED, EXPANDED, EXPANDED, UNTOUCHED, REFUSED_W, REFUSED_W])
        plans = result['plans']
        self.assertLess(2 * plans[0]['a'], 3.0); self.assertLess(2 * plans[0]['b'], 1.0)
        self.assertGreater(plans[1]['s1'], 1.0); self.assertGreater(plans[1]['s2'], 1.0)
        self.assertEqual(plans[2]['s1'], 1.0); self.assertGreater(plans[2]['s2'], 1.0)
        self.assertGreaterEqual(2 * plans[3]['a'], L_DEFAULT); self.assertGreaterEqual(2 * plans[3]['b'], W_DEFAULT)
        self.assertTrue(all(p['world_axis'] for p in plans[:4]), 'elongated bodies use their world axis')
        self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT, axes=[a for _, a, _, _ in specs])

    def test_end_on_bolt_becomes_a_streak_towards_its_vanishing_point(self):
        # The chase view: bolts flying along the view axis below the camera
        # project to their cross-section; the written streak is L x W with its
        # length on the line to the axis' vanishing point, in the camera plane.
        scene = Scene(seed=9)
        axis = [scene.forward[i] * math.cos(0.02) + scene.right[i] * math.sin(0.02) for i in range(3)]
        vertices = []
        depths = (400, 900, 2000, 5000)
        for depth in depths:
            vertices += bolt(scene.place(depth, (8, -30)), axis, scene.up, 6.0, 0.6, uv_scale=1.0)
        result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
        self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT, axes=[axis] * len(depths))
        vanishing = pixel(scene.rows, scene.viewport, [scene.camera[i] + 1e6 * axis[i] for i in range(3)])[0]
        period = result['stats']['period']
        for index, plan in enumerate(result['plans']):
            self.assertEqual(plan['verdict'], EXPANDED, index)
            self.assertTrue(plan['world_axis'], index)
            to_vp = (vanishing[0] - plan['qc'][0], vanishing[1] - plan['qc'][1]); k = math.hypot(*to_vp)
            self.assertGreater(abs(to_vp[0] / k * plan['e1'][0] + to_vp[1] / k * plan['e1'][1]), 0.999, 'length along the line to the vanishing point')
            after = [pixel(scene.rows, scene.viewport, p)[0] for p, _, _ in result['out'][index * period:(index + 1) * period]]
            a1, b1 = extents(after, plan['e1'])
            tol = self.fp32_tolerance(scene.rows, scene.viewport, [p for p, _, _ in result['out'][index * period:(index + 1) * period]])
            if 2 * plan['a'] < L_DEFAULT:
                self.assertAlmostEqual(2 * a1, L_DEFAULT, delta=2 * tol + 1e-3)
            if 2 * plan['b'] < W_DEFAULT:
                self.assertAlmostEqual(2 * b1, W_DEFAULT, delta=2 * tol + 1e-3)
            # The displacement lies in the camera plane: perpendicular to the clip-w direction.
            n = scene.rows[12:15]; kn = math.sqrt(sum(v * v for v in n))
            for (x, y, z, _, _, _), (q, _, _) in zip(vertices[index * period:(index + 1) * period], result['out'][index * period:(index + 1) * period]):
                delta = (q[0] - x, q[1] - y, q[2] - z)
                self.assertLessEqual(abs(sum(delta[i] * n[i] for i in range(3))) / kn, 0.05)

    def test_continuity_at_both_minimums(self):
        # Length stepping through L_min and width through W_min: the scales fall
        # to 1 continuously and an instance at or above both is untouched.
        scene = Scene(seed=5)
        px = scene.px_per_rad
        previous = None
        # 3.2 px wide: from 5 px long the body is elongated (hl >= 1.414 hw), so the length axis is its own.
        for full_px in (5.0, 8.0, 11.0, 11.9, 12.0, 12.5, 20.0):
            vertices = bolt(scene.place(1500, (5, -5)), scene.right, scene.up, full_px / 2 * 1500 / px, 1.6 * 1500 / px, uv_scale=1.0)
            result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
            plan = result['plans'][0]
            self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT, axes=[scene.right])
            self.assertEqual(plan['s2'], 1.0, 'wide enough: never widened')
            if 2 * plan['a'] >= L_DEFAULT:
                self.assertEqual(plan['verdict'], UNTOUCHED, full_px)
                self.assertTrue(all(s for _, _, s in result['out']))
            if previous is not None:
                self.assertLessEqual(plan['s1'], previous + 1e-6, 'the length scale decreases towards L')
            previous = plan['s1']
        self.assertEqual(previous, 1.0)
        previous = None
        for full_px in (0.3, 1.0, 2.0, 2.9, 3.0, 4.0):
            vertices = bolt(scene.place(1500, (5, -5)), scene.right, scene.up, 12.0 * 1500 / px, full_px / 2 * 1500 / px, uv_scale=1.0)
            result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
            plan = result['plans'][0]
            self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT, axes=[scene.right])
            self.assertEqual(plan['s1'], 1.0, 'long enough: never lengthened')
            if previous is not None:
                self.assertLessEqual(plan['s2'], previous + 1e-6, 'the width scale decreases towards W')
            previous = plan['s2']
        self.assertEqual(previous, 1.0)

    def test_end_on_at_its_vanishing_point_is_a_stable_disc(self):
        # F2: an elongated bolt seen exactly end-on at its own vanishing point
        # (axis through the camera) and a round body: no usable axis, so a
        # W x W square on the screen axes, the same on every frame whatever
        # rounding turns the 2D shape.
        scene = Scene(seed=14)
        plans = []
        for frame in range(6):
            jitter = 1e-6 * (frame - 2.5)
            centre = scene.place(2500, (0.0, 0.0))
            axis = [scene.forward[i] + jitter * scene.right[i] - 0.7 * jitter * scene.up[i] for i in range(3)]
            round_body = bolt(scene.place(2500, (40, 20)), scene.right, scene.up, 0.6, 0.6, uv_scale=1.0)
            vertices = bolt(centre, axis, scene.up, 4.0, 0.7, uv_scale=1.0) + round_body
            result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
            self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT)
            self.assertEqual(result['stats']['disc'], 2, frame)
            for index, plan in enumerate(result['plans']):
                self.assertEqual((plan['verdict'], plan['disc'], plan['e1']), (EXPANDED, 1, (1.0, 0.0)), (frame, index))
                period = result['stats']['period']
                after = [pixel(scene.rows, scene.viewport, q)[0] for q, _, _ in result['out'][index * period:(index + 1) * period]]
                a1, b1 = extents(after, (1.0, 0.0))
                self.assertAlmostEqual(2 * a1, W_DEFAULT, delta=0.05, msg='W wide')
                self.assertAlmostEqual(2 * b1, W_DEFAULT, delta=0.05, msg='W tall: no lengthening')
            plans.append([(round(p['s1'], 3), round(p['s2'], 3)) for p in result['plans']])
        self.assertEqual(len(set(map(tuple, plans))), 1, 'the same footprint on every frame')

    def test_a_write_always_moves_something(self):
        # F3: an instance whose vertices share one point is untouched (nothing to
        # scale); a zero-width line is lengthened only, never counted as widened.
        scene = Scene(seed=15)
        px = scene.px_per_rad
        point = scene.place(1800, (10, 4))
        collapsed = [(f32(point[0]), f32(point[1]), f32(point[2]), f32(u), f32(v), 0xff000000)
                     for _, _, _, u, v, _ in bolt(point, scene.right, scene.up, 1.0, 0.5, uv_scale=1.0)]
        line = [(x, y, z, u, v, c) for x, y, z, u, v, c in bolt(scene.place(1800, (-10, -4)), scene.right, scene.up, 2.0 * 1800 / px, 0.0, uv_scale=1.0)]
        result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, collapsed + line)
        s = result['stats']
        self.assertEqual((s['instances'], s['expanded'], s['untouched'], s['lengthened'], s['widened']), (2, 1, 1, 1, 0))
        self.assertEqual(result['plans'][0]['verdict'], UNTOUCHED)
        self.assertEqual((result['plans'][1]['s2'], result['plans'][1]['verdict']), (1.0, EXPANDED))
        self.assertEqual(result['written'], 1)
        self.check_instances(scene, result, collapsed + line, W_DEFAULT, L_DEFAULT)

    def test_view_gate_is_frame_stamped(self):
        # F1: open only on a frame whose last admitted visit wrote the chase pose:
        # written; no visit (stale); written; first-person visit; no visit;
        # written; written; site not installed; no visit after a written frame.
        self.assertEqual(Harness.run('gate').split()[1:], ['1', '0', '1', '0', '0', '1', '1', '0', '0'])

    def test_first_person_sizes_untouched_byte_for_byte(self):
        # First-person bolts near the camera: tens of pixels long and at least
        # W wide; the rule writes none of them (the chase-view gate in the
        # proxy keeps every first-person draw native in any case).
        scene = Scene(seed=8)
        vertices = []
        for k in range(6):
            vertices += bolt(scene.place(200 + 40 * k, (2 * k, -k)), scene.right, scene.up, 12.0, 0.8, uv_scale=1.0)
        result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
        self.assertEqual(result['stats']['expanded'], 0)
        self.assertEqual(result['stats']['untouched'], 6)
        self.assertEqual(result['written'], 0)
        self.assertTrue(all(s for _, _, s in result['out']), 'first-person bolts: every byte the game\'s')
        for p in result['plans']:
            self.assertGreaterEqual(2 * p['a'], L_DEFAULT)
            self.assertGreaterEqual(2 * p['b'], W_DEFAULT)
        self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT, axes=[scene.right] * 6)

    def test_random_instances(self):
        rng = random.Random(2026)
        seeds = (11, 12, 13)
        for seed in seeds:
            scene = Scene(seed=seed)
            vertices = []; axes = []
            for k in range(40):
                depth = rng.choice((rng.uniform(80, 400), rng.uniform(400, 3000), rng.uniform(3000, 30000)))
                axis = rng.choice((scene.forward, scene.right, [rng.uniform(-1, 1) for _ in range(3)]))
                half_len = rng.uniform(0.5, 12.0); half_w = rng.uniform(0.1, 1.0)
                lateral = (rng.uniform(-0.4, 0.4) * depth, rng.uniform(-0.25, 0.25) * depth)
                vertices += bolt(scene.place(depth, lateral), axis, scene.up, half_len, half_w, uv_scale=1.0)
                axes.append(axis)
            w_px, l_px = rng.choice(((3.0, 12.0), (2.0, 6.0), (4.0, 16.0)))
            result = Harness.draw(scene.rows, scene.viewport, w_px, l_px, vertices)
            self.assertEqual(result['frame'], 'ok')
            self.assertEqual(result['stats']['instances'], 40)
            self.assertGreater(result['stats']['expanded'], 0)
            self.check_instances(scene, result, vertices, w_px, l_px, axes=axes)
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
        result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
        self.assertEqual(result['frame'], 'ok')
        self.assertEqual(result['stats']['instances'], 30)
        self.assertGreater(result['stats']['expanded'], 0)
        # At 8e5 the 0.0625 m quantum reshapes 0.1 m-wide bodies: the length axis
        # is checked against the core's own definition on the stored f32 vertices.
        period = result['stats']['period']
        axes = [surface_axis([v[:3] for v in vertices[k * period:(k + 1) * period]]) for k in range(30)]
        self.check_instances(scene, result, vertices, W_DEFAULT, L_DEFAULT, axes=axes)
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
        result = Harness.draw(rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
        self.assertEqual(result['frame'], 'not_perspective')
        self.assertEqual(result['stats']['ok'], 0)
        rows = list(scene.rows); rows[12] = rows[13] = rows[14] = 0.0
        self.assertEqual(Harness.draw(rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)['frame'], 'degenerate')
        rows = list(scene.rows); rows[5] = float('nan')
        self.assertEqual(Harness.draw(rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)['frame'], 'nonfinite')
        self.assertEqual(Harness.draw(scene.rows, (0, 0, 0, 1080), W_DEFAULT, L_DEFAULT, vertices)['frame'], 'viewport')

    def test_remapped_stream_leaves_the_draw_untouched(self):
        scene = Scene(seed=4)
        vertices = []
        for k in range(3):
            vertices += bolt(scene.place(3000, (k, 0)), scene.forward, scene.up, 2.0, 0.5, cards=14, uv_scale=1.0 + 0.01 * k)
        result = Harness.draw(scene.rows, scene.viewport, W_DEFAULT, L_DEFAULT, vertices)
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
                       'native<SetStreamFn>(SetStreamSource)(device_, 0, bolt_vb_, 0, stride)', 'if (!stats.expanded) { ++c.untouched; return; }',
                       'const bool chase_view = chase_camera::pose_applied_since(chase_pose_mark_);', 'histogram_draw(frame, positions, extras, count, &bolt_hist_[1])',
                       '&stats, &bolt_hist_[0])'):
            self.assertIn(needle, prepare)
        # The view gate: outside the chase view nothing is created, locked or bound.
        gate_at = prepare.index('if (!chase_view) {')
        self.assertLess(gate_at, prepare.index('plan_draw('), 'gated views run no plan (F4)')
        for later in ('ensure_bolt_buffer(bytes)', 'bolt_vb_->Lock(', 'SetStreamSource)(device_, 0, bolt_vb_'):
            self.assertLess(gate_at, prepare.index(later), later)
        chase = (ROOT / 'src/proxy/chase_camera.cpp').read_text()
        self.assertIn('return pose_gate_open(site.patched_in, pose_written.load(std::memory_order_relaxed), pose_writes.load(std::memory_order_relaxed), mark);', chase)
        publish = chase[chase.index('void publish_pose('):chase.index('void handle(')]
        self.assertIn('    pose_written.store(written, std::memory_order_relaxed);\n    if (written) pose_writes.fetch_add(1, std::memory_order_relaxed);', publish)
        self.assertIn('if (bolt_footprint_requested_) chase_pose_mark_ = chase_camera::pose_write_count();', motion, 'the mark is taken at Present')
        self.assertIn('if (!active) { chase_fire::invalidate_camera(cockpit); return; }', chase, 'inactive cockpits never publish')
        self.assertIn('bolt_footprint_hist device=%llu', motion)
        self.assertIn('if (++screen_additive_window_frames_ >= 300u) log_screen_additive_window();', motion)
        self.assertIn('screen_emission_additive_refused_window device=%llu', motion)
        self.assertIn('if (reason < reason_count) ++screen_additive_window_.refused[reason];', additive)
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
        # Shape refusal telemetry: the sub-clause mask on the refusal branch only, the window's max/OR, the
        # row's primitives and stream-0 size (GetDesc only inside the once-per-reason log path).
        shape_at = prepare.index('if (call.topology != D3DPT_TRIANGLELIST || call.first != 0 || call.primitives > max_vertices / 3u')
        branch = prepare[shape_at:prepare.index('refuse_once(0, bits); return;', shape_at)]
        for bit in ("(call.topology != D3DPT_TRIANGLELIST ? 1u : 0u)", "(call.first != 0 ? 2u : 0u)", "(call.primitives > max_vertices / 3u ? 4u : 0u)",
                    "(!shadow_.stream0 || !shadow_.stream0_identity ? 8u : 0u)", "(shadow_.stream0_stride != stride ? 16u : 0u)",
                    "(shadow_.stream0_offset != 0 ? 32u : 0u)", "(!shadow_.declaration ? 64u : 0u)",
                    "(shadow_.position_offset != 0 || shadow_.position_type != D3DDECLTYPE_FLOAT3 ? 128u : 0u)"):
            self.assertIn(bit, branch)
        self.assertIn('++c.refused_shape; c.refused_shape_bits |= bits;', branch)
        self.assertIn('if (call.primitives > c.refused_max_prims) c.refused_max_prims = std::uint32_t(call.primitives);', branch)
        once = prepare[prepare.index('const auto refuse_once = [&]'):shape_at]
        self.assertLess(once.index('bolt_refusal_logged_ |= 1u << reason;'), once.index('GetStreamSource)(device_, 0, &bound'), 'the size query runs on the logged refusal only')
        self.assertIn('bound->GetDesc(&desc)', once)
        self.assertIn('release(bound);', once)
        self.assertIn('reason=%s detail=%u primitives=%u stream0_bytes=%lu', once)
        self.assertIn('buffer_bytes=%lu refused_max_prims=%u refused_shape_bits=%u"', motion)
        self.assertIn('static_cast<unsigned long>(bolt_vb_bytes_), w.refused_max_prims, w.refused_shape_bits);', motion)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('std::uint32_t refused_max_prims = 0, refused_shape_bits = 0;', header[header.index('struct BoltCounters {'):header.index('} bolt_window_{}, bolt_session_{};')])
        self.assertIn('w = BoltCounters{}; bolt_window_frames_ = 0;', motion, 'the window counters reset per window')
        self.assertNotIn('s.refused_shape_bits', motion, 'window-only fields: never accumulated into the (unlogged) session')
        self.assertNotIn('s.refused_max_prims', motion)
        self.assertIn('CreateVertexBuffer = 26', motion)
        self.assertIn('SLOT(IDirect3DDevice9Vtbl, CreateVertexBuffer, 26);', (ROOT / 'verification/probe/abi_check.cpp').read_text())

    def test_dll_gate_and_loader(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = capture[capture.index('X3M_BOLT_FOOTPRINT",setting'):capture.index('bolt_footprint_mode requested=1')]
        self.assertIn('bolt_footprint_requested=valid&&screen_emission_additive_requested&&ownership;', block)
        self.assertIn('w>0.f&&w<=64.f&&l>=w&&l<=256.f', block)
        self.assertIn('float l=12.f;', block, 'L defaults to 12 px')
        self.assertIn('if(length&&!(parsed&&w==0.f)){', block, 'an over-long value (31+ chars, unparsed) logs the refused mode line')
        self.assertIn('const bool fits=length&&length<32;', block)
        self.assertLess(capture.index('screen_emission_additive_mode requested=1'), capture.index('X3M_BOLT_FOOTPRINT",setting'), 'parsed after the additive gate it needs')
        self.assertIn('hooked.motion_output.configure_bolt_footprint(bolt_footprint_requested,bolt_footprint_w,bolt_footprint_l);', capture)
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
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '3,12')
            self.assertEqual(env['X3M_SCREEN_EMISSION_ADDITIVE'], '0')
            env = self.env(directory, *self.PREREQUISITES)
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('3,12', '0'))
            env = self.env(directory, *self.PREREQUISITES, '--screen-emission-additive', '2')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('3,12', '2.0'))

    def test_opt_out_and_custom_values(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = self.env(directory, *self.PREREQUISITES)
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '0')
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '0')
            self.assertEqual({k: v for k, v in env.items() if k != 'X3M_BOLT_FOOTPRINT'}, {k: v for k, v in baseline.items() if k != 'X3M_BOLT_FOOTPRINT'})
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '4,16')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('4,16', '1.0'), 'an explicit value implies the additive route at gain 1')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '4')
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '4,12')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '3,3')
            self.assertEqual(env['X3M_BOLT_FOOTPRINT'], '3,3', 'L = W: width only, a square minimum')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint', '2.5,6.5', '--screen-emission-additive', '2')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('2.5,6.5', '2.0'), 'a given gain is kept')
            env = self.env(directory, *self.PREREQUISITES, '--bolt-footprint')
            self.assertEqual((env['X3M_BOLT_FOOTPRINT'], env['X3M_SCREEN_EMISSION_ADDITIVE']), ('3,12', '1.0'))

    def test_vanilla_forwards_nothing_and_refuses_an_explicit_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_BOLT_FOOTPRINT', self.env(directory, vanilla=True))
            self.assertNotIn('X3M_BOLT_FOOTPRINT', self.env(directory, '--bolt-footprint', '0', vanilla=True))
            for args in (('--bolt-footprint',), ('--bolt-footprint', '3,12')):
                code, _, error = self.launch(directory, *args, vanilla=True)
                self.assertEqual(code, 2, args)
                self.assertIn('--bolt-footprint cannot be combined with --vanilla', error)

    def test_explicit_value_needs_the_additive_prerequisites(self):
        with tempfile.TemporaryDirectory() as directory:
            for missing in self.PREREQUISITES:
                code, _, error = self.launch(directory, *[a for a in self.PREREQUISITES if a != missing], '--bolt-footprint', '3,12')
                self.assertEqual(code, 2, missing)
                if missing != '--motion-output':  # --hdr's own prerequisite error comes first without it
                    self.assertIn('--bolt-footprint requires --motion-output --hdr --ownership', error)
            code, _, error = self.launch(directory, '--taa', '--object-trace', '--object-lifetime', '--hdr-tonemap', '--hdr-decode', 'gamma2.2', *self.PREREQUISITES, '--screen-emission', '--bolt-footprint', '3,12')
            self.assertEqual(code, 2)
            self.assertIn('--bolt-footprint needs the additive bullets', error)
            self.assertEqual(self.env(directory, '--bolt-footprint', '0')['X3M_BOLT_FOOTPRINT'], '0', 'the off value needs nothing')

    def test_malformed_values_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for bad in ('2,1', 'x', '0,5', '70', '3,300', '1,2,3', '3,', ',8', 'nan', 'inf,9', '-1', '20'):
                code, _, error = self.launch(directory, *self.PREREQUISITES, '--bolt-footprint', bad)
                self.assertEqual(code, 2, bad)
                self.assertIn('--bolt-footprint expects W or W,L', error)

    def test_help_names_the_default_and_the_gates(self):
        source = (ROOT / 'tools/manage.py').read_text()
        self.assertIn("parser.add_argument('--bolt-footprint', nargs='?', const=BOLT_FOOTPRINT_DEFAULT, default=None, metavar='W[,L]'", source)
        self.assertIn("BOLT_FOOTPRINT_DEFAULT = '3,12'", source)
        for phrase in ('launcher default on modded launches: 3,12', '--bolt-footprint 0 = off', 'X3M_BOLT_FOOTPRINT=W[,L]', 'docs/architecture/bolt-footprint.md',
                       'chase view only'):
            self.assertIn(phrase, source)


if __name__ == '__main__':
    unittest.main()
