#!/usr/bin/env python3
"""Plume look review fix F7 (2026-10-03): the length pulse once per seed byte and frame (engine_plumes_core.h
PulseCache) against once per record, on the host (clang -O2), with records of DISTINCT seeds (distinct serials and node
handles, as in flight; the Wine fixture's crowd shares one seed byte, the cache's best case).

Times build() (the cache) and the same records through build_nozzle() without a cache (the pre-fix per-record pulse;
the rest of the builder is the same code) at 100 and 1,024 records: median microseconds per build of 31 x 20 builds,
and the distinct seed bytes of the set. Host-only, no Wine. Output: plume_pulse_cache_bench_out.txt beside this script.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include "engine_plumes_core.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::engine_plumes;
namespace ee = x3m::engine_effects::core;
int main() {
    View v; const float r[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; std::memcpy(v.rows, r, sizeof r);
    v.m00 = 1.7f * 1080.f / 1920.f; v.m11 = 1.7f; v.height = 1080.f; v.near_z = 1.f;
    for (const unsigned count : {100u, 1024u}) {
        std::vector<ee::Record> rs;
        std::uint32_t s = 99;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return float(s >> 8) / 16777216.f; };
        bool seen[256] = {};
        unsigned distinct = 0;
        for (unsigned i = 0; i < count; ++i) {
            ee::Record q{}; const float z = 1500.f + 28500.f * rnd();
            q.origin[0] = (rnd() - .5f) * z; q.origin[1] = (rnd() - .5f) * z * .5f; q.origin[2] = z;
            q.axis[0] = -1.f; q.size = (3.f + 57.f * rnd()) * z / (1.7f * 540.f); q.z = .25f + 1.75f * rnd(); q.s = (q.z - .25f) / 1.75f;
            q.body = -1; q.flags = std::uint16_t(unsigned(ee::white) << ee::cluster_shift);
            q.serial = std::uint64_t(i) * 7919u + 13u; q.node_handle = 0x10000u + i * 31u; q.model = 20000;
            const unsigned b = seed_byte(q); if (!seen[b]) { seen[b] = true; ++distinct; }
            rs.push_back(q);
        }
        std::vector<Vertex> vb(std::size_t(count) * 8);
        auto time = [&](bool cached) {
            std::vector<double> us;
            for (unsigned rep = 0; rep < 31; ++rep) {
                const auto a = std::chrono::steady_clock::now();
                for (unsigned k = 0; k < 20; ++k) {
                    const float seconds = float(rep * 20 + k) / 60.f;
                    if (cached) build(rs.data(), count, nullptr, v, Preset::standard, seconds, vb.data(), count, nullptr);
                    else {
                        BuildStats st{}; unsigned w = 0;
                        for (unsigned i = 0; i < count; ++i)
                            if (build_nozzle(rs[i], nullptr, v, default_look, 1.f, seconds, vb.data() + w * 8, &st)) ++w;
                    }
                }
                us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count() / 20.);
            }
            std::sort(us.begin(), us.end());
            return us[us.size() / 2];
        };
        const double per_record = time(false), cached = time(true);
        std::printf("records=%u distinct_seeds=%u per_record_us=%.2f cached_us=%.2f ratio=%.3f\n", count, distinct, per_record, cached, cached / per_record);
    }
    return 0;
}
'''


def main():
    compiler = shutil.which('clang++') or shutil.which('c++')
    with tempfile.TemporaryDirectory(prefix='x3-pulse-bench-') as work:
        source, exe = Path(work) / 'bench.cpp', Path(work) / 'bench'
        source.write_text(HARNESS)
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(ROOT / 'src/proxy'), str(source), '-o', str(exe)], check=True)
        text = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    Path(__file__).with_name('plume_pulse_cache_bench_out.txt').write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
