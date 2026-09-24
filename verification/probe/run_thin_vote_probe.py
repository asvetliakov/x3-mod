#!/usr/bin/env python3
"""Thin-vote probe: one Wine run of thin_vote_probe.exe (docs/architecture/taa-thin-geometry-alternatives.md section 3.2).

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_thin_vote_probe.py

Builds the probe with the production flags into build/thin_vote_probe/ (untracked), runs it once with the game's
d3dx9_37.dll (disassembly only; no device is created) and the reviewed pixel program from /tmp/x3-shader-sweep, and
writes verification/results/thin-vote/probe.json: the instruction slots of every program the option changes, the
per-subset histogram cost (cold and warm) and the per-draw lookup cost. Never launches the game.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import time

import bottle
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'verification/probe/thin_vote_probe.cpp'
BUILD = ROOT / 'build/thin_vote_probe'
EXE = BUILD / 'thin_vote_probe.exe'
PROGRAM = Path('/tmp/x3-shader-sweep/programs/ps_8759c7838bbc86c2.bin')
OUT = ROOT / 'verification/results/thin-vote/probe.json'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)


def instructions(lines):
    return [l for l in lines if l.strip() and not l.strip().startswith(('//', 'ps_', 'def ', 'dcl'))]


def keep_fragments():
    """Our authored fragments' lines out of the two variants' listings (the original program's lines never leave the
    build directory): the DEFs of the pixel ABI range c216-c223, the declarations the variant adds and the instructions
    after the original's last one (the motion fragment, then the depth fragment)."""
    original = (BUILD / 'ps_8759c7838bbc86c2_original.asm').read_text().splitlines()
    kept = {}
    for name in ('off', 'thin'):
        listing = (BUILD / f'ps_8759c7838bbc86c2_variant_{name}.asm').read_text().splitlines()
        added = len(instructions(listing)) - len(instructions(original))
        defs = [l.strip() for l in listing if l.strip().startswith('def c') and 216 <= int(l.split()[1].strip(',')[1:]) < 224]
        declarations = [l.strip() for l in listing if l.strip().startswith('dcl') and l not in original]
        body = [l.strip() for l in instructions(listing)[-added:]]
        text = '\n'.join(['// the motion and depth fragments of the ps 8759c7838bbc86c2 variant, thin vote ' + name +
                           ' (verification/probe/run_thin_vote_probe.py; D3DXDisassembleShader of the game d3dx9_37)', *defs, *declarations, *body]) + '\n'
        path = ROOT / f'verification/results/thin-vote/motion_fragment_{name}.txt'
        path.write_text(text)
        kept[name] = {'defs': len(defs), 'declarations': len(declarations), 'instructions': len(body), 'file': str(path.relative_to(ROOT))}
    return kept


def main():
    if game_running():
        raise SystemExit('X3AP running or process inventory failed')
    assert PROGRAM.is_file() and sha(PROGRAM) == '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0', 'reviewed pixel program missing'
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS, str(SOURCE), str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(EXE)], check=True, cwd=ROOT)
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    overrides = 'd3dx9_37=n'
    command = [bottle.WINE, *bottle.wine_args(), '--dll', overrides, '--workdir', str(BUILD), str(EXE),
               'Z:' + str(d3dx).replace('/', '\\'), 'Z:' + str(PROGRAM).replace('/', '\\')]
    start = time.monotonic()
    run = subprocess.run(command, capture_output=True, text=True, timeout=300, env=dict(os.environ, WINEDLLOVERRIDES=overrides))
    elapsed = round(time.monotonic() - start, 1)
    (BUILD / 'stdout.txt').write_text(run.stdout)
    (BUILD / 'stderr.txt').write_text(run.stderr)
    lines = run.stdout.splitlines()
    record = {'passed': run.returncode == 0 and 'THIN_VOTE_PROBE PASS' in lines, 'exit_code': run.returncode, 'bottle': bottle.describe(),
              'game_launched': False, 'elapsed_s': elapsed, 'source_sha256': sha(SOURCE), 'runner_sha256': sha(__file__),
              'executable_sha256': sha(EXE), 'd3dx9_37_sha256': sha(d3dx), 'flags': FLAGS,
              'slots': {f['program']: {'dwords': int(f['dwords']), 'instruction_slots': int(f['instruction_slots'])} for f in (fields(l) for l in lines if l.startswith('SLOTS '))},
              'measure': [fields(l) for l in lines if l.startswith('MEASURE ')],
              'draw': [fields(l) for l in lines if l.startswith('DRAW ')], 'qpc': [fields(l) for l in lines if l.startswith('QPC ')], 'draw_cold': [fields(l) for l in lines if l.startswith('DRAW_COLD ')],
              'drain': [fields(l) for l in lines if l.startswith('DRAIN ')]}
    record['fragments'] = keep_fragments() if record['passed'] else None
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({k: record[k] for k in ('passed', 'slots', 'measure', 'draw')}, indent=1))
    if not record['passed']:
        sys.exit(1)


if __name__ == '__main__':
    main()
