#!/usr/bin/env python3
"""Fade-owner slot probe: one Wine run of fade_owner_probe.exe (docs/architecture/fade-rt2-ownership.md).

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_fade_owner_probe.py

Builds the probe with the production flags into build/fade_owner_probe/ (untracked), runs it once with the game's
d3dx9_37.dll (disassembly only; no device is created) on the local original pixel programs of the fade-route fixture
(/tmp/x3-shader-sweep/programs), and writes verification/results/fade-rt2-owner/probe.json (the instruction slots of
the current-depth fragments and of every variant with the thin vote / fade owner off and on) and owner_fragment.txt
(our fragments' lines of the ps 8759c7838bbc86c2 owner variant; the original program's lines stay in the build
directory). Never launches the game.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import time

import bottle
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'verification/probe/fade_owner_probe.cpp'
BUILD = ROOT / 'build/fade_owner_probe'
EXE = BUILD / 'fade_owner_probe.exe'
PROGRAMS = [Path(f'/tmp/x3-shader-sweep/programs/ps_{h}.bin') for h in ('8759c7838bbc86c2', '517540ae6d5e5410', 'fffdabd910793aba', '63f96eba9eea7880')]
OUT = ROOT / 'verification/results/fade-rt2-owner'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)


def instructions(lines):
    return [l.strip() for l in lines if l.strip() and not l.strip().startswith(('//', 'ps_', 'def ', 'dcl'))]


def keep_fragment():
    """The owner variant's added lines (the DEFs of c216-c223, the added declarations and the instructions after the
    original's last one: the motion fragment, then the depth fragment) of ps 8759c7838bbc86c2."""
    original = (BUILD / 'ps_8759c7838bbc86c2_original.asm').read_text().splitlines()
    kept = {}
    for mode in ('thin', 'owner'):
        listing = (BUILD / f'ps_8759c7838bbc86c2_variant_{mode}.asm').read_text().splitlines()
        added = len(instructions(listing)) - len(instructions(original))
        defs = [l.strip() for l in listing if l.strip().startswith('def c') and 216 <= int(l.split()[1].strip(',')[1:]) < 224]
        declarations = [l.strip() for l in listing if l.strip().startswith('dcl') and l not in original]
        kept[mode] = dict(defs=defs, declarations=declarations, body=instructions(listing)[-added:])
    lines = ['// the motion and depth fragments of the ps 8759c7838bbc86c2 variant with the fade owner on, and the thin-vote variant\'s'
             ' for comparison (verification/probe/run_fade_owner_probe.py; D3DXDisassembleShader of the game d3dx9_37)']
    for mode in ('owner', 'thin'):
        lines += [f'// {mode}', *kept[mode]['defs'], *kept[mode]['declarations'], *kept[mode]['body']]
    (OUT / 'owner_fragment.txt').write_text('\n'.join(lines) + '\n')
    return {m: dict(defs=len(k['defs']), declarations=len(k['declarations']), instructions=len(k['body'])) for m, k in kept.items()} | {
        'defs_identical': kept['owner']['defs'] == kept['thin']['defs'],
        'motion_body_identical': kept['owner']['body'][:-5] == kept['thin']['body'][:-4]}


def main():
    if game_running():
        raise SystemExit('X3AP running or process inventory failed')
    assert all(p.is_file() for p in PROGRAMS), 'local original pixel programs missing under /tmp/x3-shader-sweep/programs'
    BUILD.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS, str(SOURCE), str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(EXE)], check=True, cwd=ROOT)
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    overrides = 'd3dx9_37=n'
    command = [bottle.WINE, *bottle.wine_args(), '--dll', overrides, '--workdir', str(BUILD), str(EXE),
               'Z:' + str(d3dx).replace('/', '\\')] + ['Z:' + str(p).replace('/', '\\') for p in PROGRAMS]
    start = time.monotonic()
    run = subprocess.run(command, capture_output=True, text=True, timeout=300, env=dict(os.environ, WINEDLLOVERRIDES=overrides))
    elapsed = round(time.monotonic() - start, 1)
    (BUILD / 'stdout.txt').write_text(run.stdout)
    (BUILD / 'stderr.txt').write_text(run.stderr)
    lines = run.stdout.splitlines()
    passed = run.returncode == 0 and any(l.startswith('RESULT PASS') for l in lines)
    slots = {fields(l)['program']: int(fields(l)['instruction_slots']) for l in lines if l.startswith('SLOTS ')}
    refused = [fields(l) for l in lines if l.startswith('VARIANT ')]
    result = dict(passed=passed, bottle=bottle.BOTTLE, elapsed_s=elapsed, exe_sha256=sha(EXE), inputs={p.name: sha(p) for p in PROGRAMS},
                  d3dx9_37_sha256=sha(d3dx), slots=slots, refused_variants=refused, fragment=keep_fragment() if passed else None)
    (OUT / 'probe.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(dict(passed=passed, slots=slots, refused=refused)))
    if not passed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
