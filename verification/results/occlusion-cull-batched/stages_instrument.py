#!/usr/bin/env python3
"""Per-stage timing of the batched block (plan, targets, getters, Lock, set, query loop, restore), for stages-<backend>.txt.

Writes an instrumented copy of src/renderer/occlusion_cull_pass.cpp to <out>/pass_stage.cpp (one STAGES row per 600
blocks, microseconds per block); the fixture is then built against it outside the repository and run through the
runner under the Wine lock:
    python3 verification/results/occlusion-cull-batched/stages_instrument.py <out>
    i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
        -DWIN32_LEAN_AND_MEAN -DNOMINMAX verification/probe/occlusion_cull_fixture.cpp <out>/pass_stage.cpp \
        -static -static-libgcc -static-libstdc++ -luser32 -o <dir>/occlusion_cull_fixture.exe
    X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py \
        --backend wined3d --name scratch-stage-wined3d --exe <dir>/occlusion_cull_fixture.exe
    grep -E '^(STAGES|COST)' verification/results/occlusion-cull-batched/scratch-stage-wined3d.txt
The 2026-10-08 rows (stages-wined3d.txt) were taken with the dynamic/managed/discard modes of that day's pass.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    s = (ROOT / 'src/renderer/occlusion_cull_pass.cpp').read_text()
    s = s.replace('#include "occlusion_cull_pass.h"',
                  f'#include "{ROOT}/src/renderer/occlusion_cull_pass.h"\n#include <cstdio>\n'
                  'static double g_st[8]; static unsigned g_blocks;\n'
                  'static double qnow(){LARGE_INTEGER f,t;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);'
                  'return double(t.QuadPart)*1e6/double(f.QuadPart);}\n')

    def ins(after, idx):
        nonlocal s
        assert s.count(after) == 1, after
        s = s.replace(after, after + f'\n    {{ const double q = qnow(); g_st[{idx}] += q - g_mark; g_mark = q; }}')
    s = s.replace('    QueryPerformanceCounter(&t0);\n    core::PlanStats plan{};',
                  '    QueryPerformanceCounter(&t0);\n    double g_mark = qnow();\n    core::PlanStats plan{};')
    ins('    frame_.pool_truncated += plan.truncated;', 0)
    ins('    if (!targets_blend()) {\n        frame_.refused += n;\n        return done();\n    }', 1)
    ins('        hr = reinterpret_cast<GetStreamFn>(native_[GetStreamSource])(device_, 0, &stream, &offset, &stride);', 2)
    ins('write_rects(first_record, n, &base_vertex);', 3)
    ins('    step(set_rs(device_, D3DRS_DESTBLEND, D3DBLEND_ONE));', 4)
    ins('        step(end);\n    }', 5)
    ins('    if (fvf) put(reinterpret_cast<SetFvfFn>(native_[SetFVF])(device_, fvf));', 6)
    s = s.replace('    frame_.tested += n;\n',
                  '    frame_.tested += n;\n    if (++g_blocks % 600 == 0) { std::printf("STAGES blocks=600 plan=%.1f '
                  'targets=%.1f get=%.1f lock=%.1f set=%.1f loop=%.1f restore=%.1f (us per block)\\n", g_st[0]/600, '
                  'g_st[1]/600, g_st[2]/600, g_st[3]/600, g_st[4]/600, g_st[5]/600, g_st[6]/600); '
                  'for (double& v : g_st) v = 0; }\n')
    (out / 'pass_stage.cpp').write_text(s)
    print(out / 'pass_stage.cpp')


if __name__ == '__main__':
    main()
