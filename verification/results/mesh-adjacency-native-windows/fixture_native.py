#!/usr/bin/env python3
"""The D3DX mesh adjacency fixture (verification/probe/mesh_adjacency_fast_fixture.cpp) on native Windows.

Builds it with verification/probe/build_loading_trace.sh (MinGW i686) and runs it through WSL interop with the
system d3dx9_37.dll (no Wine; the Wine runner is run_loading_trace.py). Writes the summary rows (everything but
the per-case, policy, adjacency and per-state rows) to --out and exits non-zero unless the run ends with
`MESH ADJACENCY RESULT checks=<run_loading_trace.MESH_ADJACENCY_CHECKS> failures=0`.

    python3 verification/results/mesh-adjacency-native-windows/fixture_native.py [--out PATH] [--no-build]
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROBE = ROOT / 'verification/probe'
FOLDER = PROBE / 'build/mesh_adjacency_fast'
SKIP = ('ADJACENCY_CASE', 'ADJACENCY_POLICY', 'NATIVE_ADJACENCY', 'POLICY_ADJACENCY', 'FP_STATE_NATIVE', 'GAME_FP_CALL',
        'REPLAY_CASE', 'loading_', 'mesh_hook', 'RANDOM_', 'MODULE_ADJACENCY')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out', type=Path, default=Path(__file__).with_name('fixture-native.txt'))
    p.add_argument('--no-build', action='store_true')
    a = p.parse_args()
    expected = int(re.search(r'^MESH_ADJACENCY_CHECKS=(\d+)', (PROBE / 'run_loading_trace.py').read_text(), re.M)[1])
    if not a.no_build:
        subprocess.run(['sh', str(PROBE / 'build_loading_trace.sh')], check=True)
    # build_loading_trace.sh puts no DLL here: the process loads the system d3dx9_37.dll and d3d9.dll.
    assert not list(FOLDER.glob('*.dll')), 'a DLL in the fixture folder would shadow the system D3DX'
    keys = dict(X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0', X3M_ADMISSION='0')
    env = dict(os.environ, **keys, WSLENV=':'.join(keys))
    run = subprocess.run([str(FOLDER / 'mesh_adjacency_fast_fixture.exe')], cwd=FOLDER, env=env, capture_output=True,
                         text=True, timeout=600)
    rows = [l for l in run.stdout.splitlines() if not l.startswith(SKIP)]
    cases = sum(l.startswith('ADJACENCY_CASE ') for l in run.stdout.splitlines())
    terminal = f'MESH ADJACENCY RESULT checks={expected} failures=0'
    header = [f'# fixture_native.py: native Windows, system d3dx9_37.dll, rc={run.returncode}, ADJACENCY_CASE rows={cases}',
              f'# expected terminal: {terminal}']
    a.out.write_text('\n'.join(header + rows) + '\n')
    print(a.out)
    return 0 if run.returncode == 0 and rows and rows[-1].strip() == terminal else 1


if __name__ == '__main__':
    sys.exit(main())
