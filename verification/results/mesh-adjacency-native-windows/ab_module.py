#!/usr/bin/env python3
"""Before/after timing of src/proxy/mesh_adjacency_fast.cpp on native Windows (WSL interop).

Builds the benchmark (verification/probe/mesh_adjacency_native_bench.cpp, the working tree's) twice: against the
module and header of git revision REV and against the working tree's, then runs the two alternately ROUNDS times
on the named meshes and prints, per DLL and mesh, the minimum over all rounds of fast_min_us (verify policy) and
production_min_us (fast mode's policy) for each side, plus the production status and served equality.

    python3 verification/results/mesh-adjacency-native-windows/ab_module.py REV [--rounds 2] [--reps 5]
        [--meshes realistic-630,realistic-2000,...] [--out PATH]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bench  # noqa: E402

ROOT = bench.ROOT


def main():
    p = argparse.ArgumentParser()
    p.add_argument('rev')
    p.add_argument('--rounds', type=int, default=2)
    p.add_argument('--reps', type=int, default=5)
    p.add_argument('--meshes', default='realistic-630,realistic-2000,realistic-8000,split-16,fan-same-edge-2000')
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    compiler = bench.toolchain()
    if not compiler:
        sys.exit('needs i686-w64-mingw32-g++ and WSL interop')
    best = {}
    with tempfile.TemporaryDirectory(prefix='x3-adjacency-ab-') as tmp:
        # The bench includes "../../src/proxy/mesh_adjacency_fast.h" relative to itself: give each side its own tree.
        exes = {}
        for side in ('before', 'after'):
            tree = Path(tmp) / side
            (tree / 'verification/probe').mkdir(parents=True)
            (tree / 'src/proxy').mkdir(parents=True)
            (tree / 'verification/probe' / bench.SOURCE.name).write_bytes(bench.SOURCE.read_bytes())
            for name in ('mesh_adjacency_fast.h', 'mesh_adjacency_fast.cpp'):
                data = (subprocess.run(['git', 'show', f'{a.rev}:src/proxy/{name}'], cwd=ROOT, capture_output=True,
                                       check=True).stdout if side == 'before'
                        else (ROOT / 'src/proxy' / name).read_bytes())
                (tree / 'src/proxy' / name).write_bytes(data)
            exe = tree / 'bench.exe'
            subprocess.run([compiler, *bench.FLAGS, str(tree / 'verification/probe' / bench.SOURCE.name),
                            str(tree / 'src/proxy/mesh_adjacency_fast.cpp'), '-o', str(exe)], check=True)
            exes[side] = exe
        for _ in range(a.rounds):
            for side in ('before', 'after'):
                result = bench.run(exes[side], ['--reps', str(a.reps), '--meshes', a.meshes])
                if 'BENCH_DONE' not in result.stdout:
                    sys.exit(f'{side} failed rc={result.returncode}\n{result.stdout[-2000:]}')
                for line in result.stdout.splitlines():
                    if line.startswith('BENCH_ROW '):
                        f = bench.fields(line)
                        key = (f['dll'], f['mesh'], side)
                        cur = best.setdefault(key, dict(fast=1e300, production=1e300, status=f['production_status'],
                                                        served=f['served_equal']))
                        cur['fast'] = min(cur['fast'], float(f['fast_min_us']))
                        cur['production'] = min(cur['production'], float(f['production_min_us']))
                        cur['served'] = min(cur['served'], f['served_equal'])
    lines = [f'# ab_module.py {a.rev} --rounds {a.rounds} --reps {a.reps} --meshes {a.meshes}',
             '# minimum over rounds of the per-run minima, microseconds; before = module at REV, after = working tree',
             '| dll | mesh | fast_before | fast_after | production_before | production_after | status_before | status_after | served_equal |',
             '|---|---|---|---|---|---|---|---|---|']
    for dll, mesh in sorted({(k[0], k[1]) for k in best}):
        b, c = best[(dll, mesh, 'before')], best[(dll, mesh, 'after')]
        lines.append(f'| {dll} | {mesh} | {b["fast"]:.1f} | {c["fast"]:.1f} | {b["production"]:.1f} | {c["production"]:.1f} '
                     f'| {b["status"]} | {c["status"]} | {min(b["served"], c["served"])} |')
    text = '\n'.join(lines) + '\n'
    if a.out:
        a.out.write_text(text)
    print(text, end='')
    return 0


if __name__ == '__main__':
    sys.exit(main())
