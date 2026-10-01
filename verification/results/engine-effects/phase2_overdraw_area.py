#!/usr/bin/env python3
"""Engine plumes, phase 2 review fix 7 (docs/verification/engine-effects.md): the rasterised area of the stage's quads
(the pixel-program invocations, overdraw included) before and after the overdraw reduction, for the timing crowd of
verification/probe/engine_plumes_fixture.cpp (30 / 100 nozzles at 1920x1080 and 5120x1440, the same generator, view
and preset). Each nozzle's two quads are projected, clipped to the screen and their areas summed on the CPU (host
clang -O2, deterministic). "before" is engine_plumes_core.h at the given commit (default f8568972, the phase-2 head
the fix started from); "after" is the tree's.

  python3 verification/results/engine-effects/phase2_overdraw_area.py [--before <commit>]
"""
import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

DRIVER = r'''
#include "engine_plumes_core.h"
#include <cmath>
#include <cstdio>
#include <vector>
namespace ep = x3m::engine_plumes;
namespace ee = x3m::engine_effects::core;
const float m11 = 1.7f;
ee::Record record(float x, float y, float z, float ax, float ay, float az, float value, float zscale) {
    ee::Record r{};
    const float n = std::sqrt(ax * ax + ay * ay + az * az);
    r.origin[0] = x; r.origin[1] = y; r.origin[2] = z;
    r.axis[0] = ax / n; r.axis[1] = ay / n; r.axis[2] = az / n;
    r.size = value; r.z = zscale;
    float s = (zscale - .25f) / 1.75f;
    r.s = s < 0.f ? 0.f : s > 1.f ? 1.f : s;
    r.ratio = zscale; r.node_handle = 0x1234; r.model = 20000; r.body = -1;
    r.flags = std::uint16_t(unsigned(ee::white) << ee::cluster_shift);
    return r;
}
struct P { double x, y; };
double clipped_area(std::vector<P> poly, double w, double h) {
    auto clip = [](const std::vector<P>& in, int axis, double bound, bool keep_less) {
        std::vector<P> out;
        for (std::size_t i = 0; i < in.size(); ++i) {
            const P a = in[i], b = in[(i + 1) % in.size()];
            const double va = axis ? a.y : a.x, vb = axis ? b.y : b.x;
            const bool ia = keep_less ? va <= bound : va >= bound, ib = keep_less ? vb <= bound : vb >= bound;
            if (ia) out.push_back(a);
            if (ia != ib) {
                const double t = (bound - va) / (vb - va);
                out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
            }
        }
        return out;
    };
    poly = clip(poly, 0, 0, false); if (poly.size() < 3) return 0;
    poly = clip(poly, 0, w, true); if (poly.size() < 3) return 0;
    poly = clip(poly, 1, 0, false); if (poly.size() < 3) return 0;
    poly = clip(poly, 1, h, true); if (poly.size() < 3) return 0;
    double a = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P p = poly[i], q = poly[(i + 1) % poly.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return std::fabs(a) * .5;
}
int main() {
    for (const auto size : {std::pair<unsigned, unsigned>{1920u, 1080u}, std::pair<unsigned, unsigned>{5120u, 1440u}}) {
        const float w = float(size.first), h = float(size.second), m00 = m11 * h / w;
        for (const unsigned n : {30u, 100u}) {
            std::vector<ee::Record> records;
            std::uint32_t seed = 12345;
            auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.f; };
            for (unsigned i = 0; i < n; ++i) {
                const float z = 1500.f + 28500.f * rnd() * rnd();
                const float x = (rnd() * 1.6f - .8f) * z / m00, y = (rnd() * 1.6f - .8f) * z / m11;
                const float px = 3.f + 57.f * rnd() * rnd();
                const float ppu = m11 * h * .5f / z;
                records.push_back(record(x, y, z, rnd() - .5f, rnd() - .5f, rnd() - .5f, px / ppu, .25f + 1.75f * rnd()));
            }
            ep::View view{};
            const float rows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            for (unsigned i = 0; i < 12; ++i) view.rows[i] = rows[i];
            view.m00 = m00; view.m11 = m11; view.height = h; view.near_z = 1.f;
            std::vector<ep::Vertex> out(std::size_t(n) * ep::vertices_per_nozzle);
            ep::BuildStats st{};
            const unsigned drawn = ep::build(records.data(), n, nullptr, view, ep::Preset::standard, 0, out.data(), n, &st);
            double axial = 0, disc = 0;
            for (unsigned k = 0; k < drawn * 2; ++k) {
                std::vector<P> poly;
                for (unsigned c : {0u, 1u, 3u, 2u}) {
                    const ep::Vertex& v = out[k * 4 + c];
                    poly.push_back({(m00 * v.position[0] / v.position[2] + 1.) * w * .5,
                                    (1. - m11 * v.position[1] / v.position[2]) * h * .5});
                }
                (k % 2 ? disc : axial) += clipped_area(poly, w, h);
            }
            std::printf("AREA width=%u height=%u nozzles=%u drawn=%u discs=%u axial_px=%.0f disc_px=%.0f total_px=%.0f\n",
                        size.first, size.second, n, drawn, st.discs, axial, disc, axial + disc);
        }
    }
}
'''


def run(header_dir, work):
    exe = work / 'area'
    (work / 'driver.cpp').write_text(DRIVER)
    compiler = shutil.which('clang++') or shutil.which('c++')
    subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(header_dir), '-I', str(ROOT / 'src/proxy'),
                    str(work / 'driver.cpp'), '-o', str(exe)], check=True)
    rows = []
    for line in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines():
        rows.append({k: (int(v) if v.isdigit() else v) for k, v in (f.split('=') for f in line.split()[1:])})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--before', default='f8568972')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='x3-plume-area-') as directory:
        work = Path(directory)
        before_dir = work / 'before'
        before_dir.mkdir()
        old = subprocess.run(['git', '-C', str(ROOT), 'show', f'{args.before}:src/proxy/engine_plumes_core.h'],
                             capture_output=True, text=True, check=True).stdout
        (before_dir / 'engine_plumes_core.h').write_text(old)
        (work / 'b').mkdir()
        (work / 'a').mkdir()
        before = run(before_dir, work / 'b')
        after = run(ROOT / 'src/proxy', work / 'a')
    out = []
    for b, a in zip(before, after):
        out.append({'width': b['width'], 'height': b['height'], 'nozzles': b['nozzles'], 'drawn': [b['drawn'], a['drawn']],
                    'discs': [b['discs'], a['discs']], 'axial_px': [b['axial_px'], a['axial_px']],
                    'disc_px': [b['disc_px'], a['disc_px']], 'total_px': [b['total_px'], a['total_px']],
                    'ratio': round(a['total_px'] / b['total_px'], 3) if b['total_px'] else None})
    print(json.dumps({'before': args.before, 'cases': out}, indent=1))


if __name__ == '__main__':
    main()
