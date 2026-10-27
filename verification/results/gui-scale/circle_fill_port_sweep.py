#!/usr/bin/env python3
"""Equivalence sweep of the oversized-disc port (src/proxy/circle_fill_sites.h fill()) against the unbounded emulation.

  python3 verification/results/gui-scale/circle_fill_port_sweep.py > verification/results/gui-scale/circle_fill_port_sweep.txt

Compiles the HARNESS of verification/analysis/test_circle_fill.py with the host C++ compiler (-O2) and compares every
case span for span with circlefill(..., wrap=False) of verification/results/gui-scale/sector_map_scanner_range.py,
clipped to the texture when B > A (the port also clips then): r = 814..2599 centred, 150 random r up to 16000, 1500
random clipped placements on 512..2048 textures, 300 random aspects 20000..300000; random.Random(1). A decline of the
port must match the B range rule. No Wine, no game.
"""
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import test_circle_fill as t  # noqa: E402

MAX_B = 32767 * 32767


def b_of(r, aspect):
    q = ((aspect * aspect + 0x8000) >> 16) & 0xffffffff
    q = q - (1 << 32) if q & 0x80000000 else q
    if q <= 0:
        return None
    b = (r * r << 16) // q
    return b if b <= MAX_B else None


rng = random.Random(1)
groups = {'centred r 814..2599': [(3000, 3000, r, 6000, 6000, 65536) for r in range(814, 2600)],
          'random r up to 16000': [(16383, 16383, rng.randint(814, 16000), 32766, 32766, 65536) for _ in range(150)],
          'random clipped on 512..2048': [], 'random aspect 20000..300000': []}
for _ in range(1500):
    w, h = rng.randint(512, 2048), rng.randint(512, 2048)
    r = rng.randint(814, 4000)
    groups['random clipped on 512..2048'].append((rng.randint(-r // 2, w + r // 2), rng.randint(-r // 2, h + r // 2), r, w, h, 65536))
for _ in range(300):
    groups['random aspect 20000..300000'].append((3000, 3000, rng.randint(814, 2599), 6000, 6000, rng.randint(20000, 300000)))
cases = [c for g in groups.values() for c in g]

compiler = shutil.which('clang++') or shutil.which('c++')
with tempfile.TemporaryDirectory(prefix='x3-circle-fill-sweep-') as directory:
    source, executable = Path(directory) / 'harness.cpp', Path(directory) / 'harness'
    source.write_text(t.HARNESS)
    subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(source), '-o',
                    str(executable)], check=True)
    run = subprocess.run([str(executable)], input=''.join(' '.join(map(str, c)) + '\n' for c in cases), capture_output=True, text=True,
                         check=True)
results, current = [], None
for line in run.stdout.splitlines():
    if line.startswith(('modes', 'classes')):
        continue
    if line == 'decline':
        current = None
    elif line.startswith('B '):
        current = {}
    elif line == 'end':
        results.append(current)
    else:
        y, x0, x1 = map(int, line.split())
        current[y] = (x0, x1)
assert len(results) == len(cases), (len(results), len(cases))

circlefill = t.emulation()
version = subprocess.run([compiler, '--version'], capture_output=True, text=True).stdout.splitlines()[0]
print(f'compiler: {version}; seed 1; {len(cases)} cases')
index, total_bad = 0, 0
for name, group in groups.items():
    same = declined = bad = spans = 0
    for case in group:
        port = results[index]
        index += 1
        cx, cy, r, w, h, aspect = case
        if port is None:
            if b_of(r, aspect) is None:
                declined += 1
            else:
                bad += 1
                print('  unexpected decline', case)
            continue
        expected = t.expected_spans(circlefill, cx, cy, r, w, h, aspect)
        spans += len(expected)
        if port == expected:
            same += 1
        else:
            bad += 1
            print('  MISMATCH', case)
    total_bad += bad
    print(f'{name}: {len(group)} cases, {same} identical span for span ({spans} rows), {declined} declined by the B rule, {bad} mismatches')
print('RESULT', 'PASS' if total_bad == 0 else 'FAIL')
