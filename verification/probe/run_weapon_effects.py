#!/usr/bin/env python3
"""Weapon effects shield-hit D3D fixture (docs/architecture/weapon-effects.md 5.1 acceptance (b)).

Builds verification/probe/weapon_effects_fixture.cpp with the production WeaponEffectsPass and its shell/decal programs
(src/renderer/weapon_effects_pass.cpp; refusing stale embedded programs: each weapon-{shell,decal}-*-program.json must
match its source and header), runs it once and writes the record: the build, the checkout's commit and the production
sources' hashes, every CHECK, the structural metrics (rim at 70 vs 45 deg, rings on/off, hull cut, decal over sky vs
hull, shell core, the age law, the retired-owner case, Reset and the fault cases) and the stage cost at 1080p and
5120x1440 as the native re-pin of the dropped ledger's Wine numbers. Never launches the game.

Two hosts, chosen explicitly: --host native (native_host.py; the WSL2 host, no bottle) runs the binary on the Windows
side through WSL interop with the system d3d9.dll and the game's C:\\X3\\d3dx9_37.dll, into
verification/results/native-windows/weapon-effects/ with the host identity block; --host bottle runs it in the X3
CrossOver bottle (builtin d3d9). Either way through wine_lock.py (one fixture at a time). There is no fallback.
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import bottle
import native_host
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/weapon-effects'
EXE = BUILD / 'weapon_effects_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++',
         '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DX3M_WEAPON_EFFECTS_FIXTURE']
SOURCES = ('verification/probe/weapon_effects_fixture.cpp', 'src/renderer/weapon_effects_pass.cpp')
PRODUCTION_SOURCES = ('src/renderer/weapon_effects_pass.h', 'src/renderer/weapon_effects_pass.cpp',
                      'src/proxy/weapon_effects_core.h', 'src/effects/weapon_shell_vs.hlsl', 'src/effects/weapon_shell_ps.hlsl',
                      'src/effects/weapon_decal_vs.hlsl', 'src/effects/weapon_decal_ps.hlsl', 'src/effects/weapon_soft_depth.hlsl',
                      'src/renderer/weapon_shell_vertex_program_inc.h', 'src/renderer/weapon_shell_pixel_program_inc.h',
                      'src/renderer/weapon_decal_vertex_program_inc.h', 'src/renderer/weapon_decal_pixel_program_inc.h')
PROGRAMS = {
    'weapon_shell_vs': ('verification/results/weapon-shell-vertex-program.json', 'src/renderer/weapon_shell_vertex_program_inc.h'),
    'weapon_shell_ps': ('verification/results/weapon-shell-pixel-program.json', 'src/renderer/weapon_shell_pixel_program_inc.h'),
    'weapon_decal_vs': ('verification/results/weapon-decal-vertex-program.json', 'src/renderer/weapon_decal_vertex_program_inc.h'),
    'weapon_decal_ps': ('verification/results/weapon-decal-pixel-program.json', 'src/renderer/weapon_decal_pixel_program_inc.h'),
}
# The dropped effects-stage ledger's Wine/wined3d numbers (docs/verification/effects-stage.md, measured on the Mac),
# re-pinned natively here; the fixture gates the structural relations, not these values.
WINE_PINS = {'rim_70': 0.50, 'rim_45': 0.24, 'hull_cut': 0.0, 'two_shells_1080_ms': 0.124, 'two_shells_5120_ms': 0.162}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_binding():
    def git(*a):
        return subprocess.run(['git', '-C', str(ROOT), *a], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {p: sha(ROOT / p) for p in PRODUCTION_SOURCES}}


def programs_current():
    out = {}
    for name, (record_path, header) in PROGRAMS.items():
        record = json.loads((ROOT / record_path).read_text())
        if record['source_sha256'] != sha(ROOT / record['source']) or record['header_sha256'] != sha(ROOT / header):
            raise SystemExit(f'{name}: stale embedded program; run tools/shaders/generate_weapon_effects_programs.py')
        out[name] = {k: record[k] for k in ('source', 'word_count', 'bytecode_sha256', 'target')}
    return out


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    command = ['i686-w64-mingw32-g++', *FLAGS, *(str(ROOT / s) for s in SOURCES), '-o', str(EXE), '-luser32']
    done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (BUILD / 'build.log').write_text(done.stdout + done.stderr)
    if done.returncode:
        raise SystemExit('fixture build failed:\n' + done.stdout + done.stderr)
    return {'command': [str(c) for c in command], 'warnings': len(re.findall(r'\bwarning:', done.stdout + done.stderr)),
            'executable_sha256': sha(EXE)}


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse(text):
    tags = ('ATTACH', 'SHELL', 'RIM', 'RINGS', 'HULL', 'DECAL', 'AGE', 'RETIRED', 'TIMING')
    report = {t.lower(): [] for t in tags}
    report.update(checks=[], result=None)
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'CHECK':
            parts = line.split()
            report['checks'].append([parts[1], parts[2] == 'PASS'])
        elif head in tags:
            report[head.lower()].append(fields(line))
        elif head == 'RESULT':
            report['result'] = dict(fields(line), verdict=line.split()[1])
    report['failed_checks'] = [n for n, ok in report['checks'] if not ok]
    return report


def run_fixture(args):
    if args.host == 'native':
        d3dx = native_host.windows_path(native_host.GAME_DIR / 'd3dx9_37.dll')
        done, text = native_host.run_native(EXE, [d3dx], BUILD, args.timeout, log=BUILD / 'fixture')
        return done.returncode, text
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(EXE),
               r'C:\X3\d3dx9_37.dll']
    done = subprocess.run(command, capture_output=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=args.timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    (BUILD / 'fixture.log').write_text(text)
    (BUILD / 'fixture.stderr.txt').write_bytes(done.stderr)
    return done.returncode, text


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--host', choices=('bottle', 'native'), default='bottle',
                        help='bottle (default): the CrossOver bottle X3M_FIXTURE_BOTTLE; native: the Windows side of this '
                             'WSL2 host through interop (native_host.py), never a fallback')
    args = parser.parse_args()
    native = args.host == 'native'
    if native:
        if native_host.missing():
            raise SystemExit('--host native needs ' + '; '.join(native_host.missing()))
        if game_running() or native_host.game_running_windows():
            raise SystemExit('the game is running')
        results = native_host.results_dir(ROOT, 'weapon-effects')
    else:
        if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
            raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
        if game_running():
            raise SystemExit('the game is running')
        results = bottle.results_dir(ROOT) / 'weapon-effects'
        results.mkdir(parents=True, exist_ok=True)
    record = {'bottle': None if native else bottle.describe(), 'production_sources': list(PRODUCTION_SOURCES),
              'source': source_binding(), 'programs': programs_current(), 'wine_pins': WINE_PINS, 'game_launched': False}
    if not args.no_build:
        record['build'] = build()
    d3dx = native_host.GAME_DIR / 'd3dx9_37.dll' if native else bottle.game_dir() / 'd3dx9_37.dll'
    record['d3dx9_37_sha256'] = sha(d3dx) if Path(d3dx).is_file() else None
    returncode, text = run_fixture(args)
    report = parse(text)
    if native:
        record['host'] = native_host.identity(text)
    record['run'] = {'exit_code': returncode, 'result': report['result'],
                     'log': str((BUILD / 'fixture.log').relative_to(ROOT))}
    record['checks'] = report['checks']
    record['failed_checks'] = report['failed_checks']
    record['metrics'] = {k: report[k] for k in ('attach', 'shell', 'rim', 'rings', 'hull', 'decal', 'age', 'retired', 'timing')}
    out_path = results / 'summary.json'
    out_path.write_text(json.dumps(record, indent=2) + '\n')
    passed = returncode == 0 and report['result'] and report['result']['verdict'] == 'PASS' and not report['failed_checks']
    print(json.dumps({'exit': returncode, 'result': report['result'], 'failed': report['failed_checks'],
                      'timing': report['timing'], 'record': str(out_path.relative_to(ROOT))}, indent=1))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
