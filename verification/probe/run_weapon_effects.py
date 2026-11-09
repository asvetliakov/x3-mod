#!/usr/bin/env python3
"""Weapon effects shield-hit fixtures (docs/architecture/weapon-effects.md 5.1 acceptance (a) and (b)).

(a) The hook fixture (verification/probe/build_weapon_effects_hook.py builds and audits it; --host native only): run on
the Windows side through native_host.py, its RESULT line and every CHECK row parsed into
verification/results/native-windows/weapon-effects/hook-summary.json with the checkout's commit, the hook sources'
hashes, the build audit and the host identity (test_weapon_effects_hook binds to it).

(b) The D3D fixture:
Builds verification/probe/weapon_effects_fixture.cpp with the production WeaponEffectsPass and its patch/decal programs
(src/renderer/weapon_effects_pass.cpp; refusing stale embedded programs: each weapon-{shell,decal}-*-program.json must
match its source and header), runs it once and writes the record: the build, the checkout's commit and the production
sources' hashes, every CHECK, the structural metrics of the hex patch (docs/architecture/weapon-effects.md 5.1.1: lit
radius and edge count per tier with the symmetry counts, the centre cell over a ring cell, ring front at two ages, the
fade, the colour bands, re-light and four-hit accumulation, the screen-space placement on hull lanes (narrow and flat
hulls from every side on the ship bubble from the production hull walk, the Flight B station on the hull decal), the
bubble's sides (the chase camera's nose hit on the far side around the outline, the eye inside the bubble), the hull
walk's cost warm and cold, with the normal errors,
the surface case, the no-pop runs (fighter and spread capital on a flat face, the one-draw eviction witness, the
sphere fronts, each with its worst pixel), the face-edge pair, the stage's CPU cost (with the Flight B station cluster), the decal fallback,
the marker cell through the production TemporalPass at 0 / 4 / 8 px per frame, the age law with the measured SETA 6 wall time, the retired-owner case through the stage builder, Reset
and the fault cases, the program-create refusal) and the stage cost at 1080p and 5120x1440 (fence, two patches on a
fighter, two on a capital at close range, the worst case of one owner); the projectile stage
(docs/architecture/projectile-look.md: the core width rule at 200 m / 500 m / 1 km / 3 km at two fields of view, SETA
invariance, the far flag, the dot LOD, nothing outside the quad, depth occlusion, the pool's draw and retirement, Reset,
the 512-bolt budget at 5120x1440; PROJECTILE rows). Never launches the game. The hull-hit, explosion
and bolt cases left with the look layer on 2026-11-08 (docs/archive/effects-look-2026-11-08/index.md); the hook
fixture keeps the bolt and death sites' hook cases.

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
import build_weapon_effects_hook as hook_build
import build_weapon_effects_hook_fl as hook_build_fl
import native_host
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/weapon-effects'
EXE = BUILD / 'weapon_effects_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++',
         '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DX3M_WEAPON_EFFECTS_FIXTURE']
SOURCES = ('verification/probe/weapon_effects_fixture.cpp', 'src/renderer/weapon_effects_pass.cpp',
           'src/renderer/temporal_pass.cpp')
# The production reader the hull walk is timed through (review F4), compiled as the DLL compiles it (CMakeLists.txt:
# no SSE / MMX) into its own object.
NO_SSE_SOURCES = ('src/proxy/engine_memory.cpp',)
NO_SSE = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
PRODUCTION_SOURCES = ('src/renderer/weapon_effects_pass.h', 'src/renderer/weapon_effects_pass.cpp',
                      'src/proxy/weapon_effects_core.h', 'src/proxy/weapon_effects_stage.h',
                      'src/proxy/weapon_effects_bubble.h', 'src/effects/weapon_bubble_ps.hlsl',
                      'src/proxy/engine_memory.h', 'src/proxy/engine_memory.cpp',
                      'src/renderer/weapon_bubble_pixel_program_inc.h',
                      'src/renderer/temporal_pass.h', 'src/renderer/temporal_pass.cpp',
                      'src/effects/weapon_shell_vs.hlsl', 'src/effects/weapon_shell_ps.hlsl',
                      'src/effects/weapon_decal_vs.hlsl', 'src/effects/weapon_decal_ps.hlsl', 'src/effects/weapon_soft_depth.hlsl',
                      'src/effects/weapon_hex_patch.hlsl',
                      'src/renderer/weapon_shell_vertex_program_inc.h', 'src/renderer/weapon_shell_pixel_program_inc.h',
                      'src/renderer/weapon_decal_vertex_program_inc.h', 'src/renderer/weapon_decal_pixel_program_inc.h',
                      # the projectile stage (docs/architecture/projectile-look.md)
                      'src/proxy/weapon_projectile_stage.h', 'src/proxy/weapon_projectile_family.h',
                      'src/proxy/weapon_effects_projectile.h', 'src/effects/weapon_projectile_vs.hlsl',
                      'src/effects/weapon_projectile_ps.hlsl', 'src/renderer/weapon_projectile_vertex_program_inc.h',
                      'src/renderer/weapon_projectile_pixel_program_inc.h',
                      # the bullet re-shade (docs/reverse-engineering/fl/bullet-draw-batch.md section 6)
                      'src/effects/weapon_bullet_reshade_ps.hlsl', 'src/renderer/weapon_bullet_reshade_pixel_program_inc.h',
                      'src/effects/weapon_bullet_reshade.hlsl', 'src/effects/weapon_bullet_reshade_flag_ps.hlsl',
                      'src/renderer/weapon_bullet_reshade_flag_pixel_program_inc.h', 'src/proxy/bullet_reshade_route.h',
                      'src/proxy/bullet_reshade_core.h', 'verification/probe/weapon_bullet_reshade_fixture.h',
                      # the muzzle flash's hull pulse (docs/architecture/muzzle-flash-look.md)
                      'src/effects/weapon_pulse_ps.hlsl', 'src/renderer/weapon_pulse_pixel_program_inc.h',
                      # the D3D fixture itself (review 2026-11-08: its lost-device guard and the shared helpers)
                      'verification/probe/weapon_effects_fixture.cpp')
PROGRAMS = {
    'weapon_shell_vs': ('verification/results/weapon-shell-vertex-program.json', 'src/renderer/weapon_shell_vertex_program_inc.h'),
    'weapon_shell_ps': ('verification/results/weapon-shell-pixel-program.json', 'src/renderer/weapon_shell_pixel_program_inc.h'),
    'weapon_decal_vs': ('verification/results/weapon-decal-vertex-program.json', 'src/renderer/weapon_decal_vertex_program_inc.h'),
    'weapon_decal_ps': ('verification/results/weapon-decal-pixel-program.json', 'src/renderer/weapon_decal_pixel_program_inc.h'),
    'weapon_bubble_ps': ('verification/results/weapon-bubble-pixel-program.json', 'src/renderer/weapon_bubble_pixel_program_inc.h'),
    'weapon_projectile_vs': ('verification/results/weapon-projectile-vertex-program.json',
                             'src/renderer/weapon_projectile_vertex_program_inc.h'),
    'weapon_projectile_ps': ('verification/results/weapon-projectile-pixel-program.json',
                             'src/renderer/weapon_projectile_pixel_program_inc.h'),
    'weapon_pulse_ps': ('verification/results/weapon-pulse-pixel-program.json', 'src/renderer/weapon_pulse_pixel_program_inc.h'),
    'weapon_bullet_reshade_ps': ('verification/results/weapon-bullet-reshade-pixel-program.json',
                                 'src/renderer/weapon_bullet_reshade_pixel_program_inc.h'),
    'weapon_bullet_reshade_flag_ps': ('verification/results/weapon-bullet-reshade-flag-pixel-program.json',
                                      'src/renderer/weapon_bullet_reshade_flag_pixel_program_inc.h'),
}
# The full-shell record this patch replaces (docs/verification/weapon-effects.md, native, commit bc30d8cf1): two shells
# and the full-screen shell, fenced (us); the patch's timing cases are reported against them, not gated.
SHELL_PINS = {'two_shells_1080_us': 61.3, 'two_shells_5120_us': 201.8, 'fullscreen_1080_us': 43.7,
              'fullscreen_5120_us': 151.9}
# The hook fixture's inputs: the fixture and its seam, the build script (flags and audit) and every production source it
# compiles; the record carries their hashes and test_weapon_effects_hook compares them with the tree.
HOOK_SOURCES = ('verification/probe/weapon_effects_hook_fixture.cpp', 'verification/probe/weapon_effects_hook_fixture_shim.h',
                'verification/probe/build_weapon_effects_hook.py', 'src/proxy/weapon_effects_patch.cpp',
                'src/proxy/weapon_effects_patch.h', 'src/proxy/weapon_effects_hook.cpp', 'src/proxy/weapon_effects_core.h',
                'src/proxy/weapon_effects_sites.h', 'src/proxy/weapon_effects_bolt.h', 'src/proxy/weapon_effects_bolt_sites.h',
                'src/proxy/weapon_effects_table.h', 'src/proxy/engine_patch.cpp', 'src/proxy/engine_patch.h',
                'src/proxy/weapon_effects_death_sites.h')
# The FL hook fixture's inputs (docs/architecture/weapon-effects.md 2.10): the FL fixture, the shared module/handler
# compiled with X3M_EDITION_FL, the FL site table, the bubble walk and the build script.
HOOK_FL_SOURCES = ('verification/probe/weapon_effects_hook_fixture_fl.cpp', 'verification/probe/weapon_effects_hook_fixture_shim.h',
                   'verification/probe/build_weapon_effects_hook_fl.py', 'src/proxy/weapon_effects_patch.cpp',
                   'src/proxy/weapon_effects_patch.h', 'src/proxy/weapon_effects_hook.cpp', 'src/proxy/weapon_effects_core.h',
                   'src/engine/fl/weapon_effects_sites_fl.h', 'src/proxy/weapon_effects_bolt.h', 'src/proxy/weapon_effects_bubble.h',
                   'src/proxy/weapon_effects_table.h', 'src/proxy/engine_patch.cpp', 'src/proxy/engine_patch.h',
                   'src/engine/fl/executable_identity_fl.h', 'src/proxy/weapon_effects_projectile.h')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_binding(paths=PRODUCTION_SOURCES):
    def git(*a):
        return subprocess.run(['git', '-C', str(ROOT), *a], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *paths).splitlines()]
    # The run's own records (written one after the other in this directory) do not make the checkout dirty.
    dirty = git('status', '--porcelain', '--untracked-files=no', '--', '.',
                ':(exclude)verification/results/native-windows/weapon-effects').strip()
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if dirty else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {p: sha(ROOT / p) for p in paths}}


def programs_current():
    out = {}
    for name, (record_path, header) in PROGRAMS.items():
        record = json.loads((ROOT / record_path).read_text())
        if record['source_sha256'] != sha(ROOT / record['source']) or record['header_sha256'] != sha(ROOT / header):
            raise SystemExit(f'{name}: stale embedded program; run tools/shaders/generate_weapon_effects_programs.py '
                             '(the re-shade: generate_bullet_reshade_program.py)')
        out[name] = {k: record[k] for k in ('source', 'word_count', 'bytecode_sha256', 'target')}
    return out


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects, logs, commands = [], '', []
    for source in NO_SSE_SOURCES:
        obj = BUILD / (Path(source).stem + '.o')
        command = ['i686-w64-mingw32-g++', *FLAGS, *NO_SSE, '-c', str(ROOT / source), '-o', str(obj)]
        done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        logs += done.stdout + done.stderr
        commands.append([str(c) for c in command])
        if done.returncode:
            raise SystemExit('fixture build failed:\n' + logs)
        objects.append(str(obj))
    command = ['i686-w64-mingw32-g++', *FLAGS, *(str(ROOT / s) for s in SOURCES), *objects, '-o', str(EXE), '-luser32',
               '-ldxguid']
    done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    logs += done.stdout + done.stderr
    (BUILD / 'build.log').write_text(logs)
    if done.returncode:
        raise SystemExit('fixture build failed:\n' + logs)
    commands.append([str(c) for c in command])
    return {'command': commands[-1], 'objects': commands[:-1], 'warnings': len(re.findall(r'\bwarning:', logs)),
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
    tags = ('ATTACH', 'PATCH', 'SYMMETRY', 'CENTRE', 'RING', 'RINGCELLS', 'FADECELL', 'COLOUR', 'RELIGHT', 'ADJACENT',
            'PLACEMENT', 'AXIS_PLANE', 'BUBBLE_SIDE', 'HULL_WALK', 'NORMALS', 'SURFACE', 'CULL', 'SORT', 'ATTACH_SLOTS', 'COCKPIT', 'GRID', 'NOPOP', 'SEAM', 'STAGE_CPU', 'TIMING_MAPS', 'DECAL', 'AGE', 'RETIRED', 'RESOLVE',
            'REFUSED', 'TIMING', 'PROJECTILE', 'FLASH', 'PULSE', 'PULSE_CAP', 'PULSE_GHOST', 'RESHADE')
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


def parse_hook(text):
    """The hook fixture's rows: `CHECK pass|FAIL <name> [detail]`, `RESULT PASS|FAIL checks= failures= faults=` and the
    FORWARD_STATE row (the entry and callee EFLAGS, ESP and the return address) and STACK_DEPTH (how far below the
    synthetic frame the stub and the handler wrote)."""
    checks, result, forward, stack = [], None, None, None
    for line in text.splitlines():
        parts = line.split()
        if parts[:1] == ['CHECK'] and len(parts) >= 3:
            checks.append({'name': parts[2], 'passed': parts[1] == 'pass', 'detail': ' '.join(parts[3:])})
        elif parts[:1] == ['RESULT'] and len(parts) >= 2:
            result = dict(fields(line), verdict=parts[1])
        elif parts[:1] == ['FORWARD_STATE']:
            forward = dict(re.findall(r'(\w+)=(\S+)', line))
        elif parts[:1] == ['STACK_DEPTH']:
            stack = fields(line)
    shields = [fields(line) for line in text.splitlines() if line.startswith('SHIELD ')]
    cost = [fields(line) for line in text.splitlines() if line.startswith('EVENT_COST ')]
    travel = [fields(line) for line in text.splitlines() if line.startswith('DIR ')]
    suppress = [fields(line) for line in text.splitlines() if line.startswith('SUPPRESS ')]
    drained = [fields(line) for line in text.splitlines() if line.startswith('DRAINED ')]
    hull_mode = [fields(line) for line in text.splitlines() if line.startswith('HULL_MODE ')]
    hull_class = [fields(line) for line in text.splitlines() if line.startswith('HULL_CLASS ')]
    overflow = [fields(line) for line in text.splitlines() if line.startswith('OVERFLOW ')]
    cost_full = [fields(line) for line in text.splitlines() if line.startswith('EVENT_COST_FULL ')]
    death_forward = next((dict(re.findall(r'(\w+)=(\S+)', line)) for line in text.splitlines()
                          if line.startswith('DEATH_FORWARD_STATE ')), None)
    death_nested = [fields(line) for line in text.splitlines() if line.startswith('DEATH_NESTED ')]
    death_state = [fields(line) for line in text.splitlines() if line.startswith('DEATH_STATE ')]
    muzzle = [fields(line) for line in text.splitlines() if line.startswith('MUZZLE ')]
    return {'checks': checks, 'result': result, 'forward_state': forward, 'stack_depth': stack, 'shield': shields,
            'dir': travel, 'suppress': suppress, 'drained': drained, 'hull_mode': hull_mode, 'hull_class': hull_class,
            'event_cost': cost[-1] if cost else None, 'overflow': overflow[-1] if overflow else None,
            'event_cost_full': cost_full[-1] if cost_full else None, 'death_forward_state': death_forward,
            'death_nested': death_nested[-1] if death_nested else None, 'death_state': death_state[-1] if death_state else None,
            'muzzle': muzzle[-1] if muzzle else None,
            'failed_checks': [c['name'] for c in checks if not c['passed']]}


def run_hook(args, results):
    """(a): build and audit the hook fixture, run it natively, write hook-summary.json; True when it passed."""
    record = {'fixture': 'weapon_effects_hook', 'bottle': None, 'game_launched': False, 'sources': list(HOOK_SOURCES),
              'source': source_binding(HOOK_SOURCES)}
    record['build'] = hook_build.build()
    record['build']['executable_sha256'] = sha(hook_build.EXE)
    done, text = native_host.run_native(hook_build.EXE, [], hook_build.BUILD, args.timeout, log=hook_build.BUILD / 'fixture')
    report = parse_hook(text)
    record['host'] = native_host.identity(text)
    record['run'] = {'exit_code': done.returncode, 'result': report['result'],
                     'log': str((hook_build.BUILD / 'fixture.log').relative_to(ROOT))}
    record['checks'] = report['checks']
    record['check_count'] = len(report['checks'])
    record['failed_checks'] = report['failed_checks']
    record['forward_state'] = report['forward_state']
    record['stack_depth'] = report['stack_depth']
    record['shield'] = report['shield']
    record['dir'] = report['dir']
    record['suppress'] = report['suppress']
    record['drained'] = report['drained']
    record['hull_mode'] = report['hull_mode']
    record['hull_class'] = report['hull_class']
    record['event_cost'] = report['event_cost']
    record['overflow'] = report['overflow']
    record['event_cost_full'] = report['event_cost_full']
    record['death_forward_state'] = report['death_forward_state']
    record['death_nested'] = report['death_nested']
    record['death_state'] = report['death_state']
    result = report['result'] or {}
    record['passed'] = (done.returncode == 0 and result.get('verdict') == 'PASS' and not report['failed_checks'] and
                        result.get('checks') == len(report['checks']) and result.get('failures') == 0)
    out_path = results / 'hook-summary.json'
    out_path.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'hook_exit': done.returncode, 'hook_result': report['result'], 'hook_failed': report['failed_checks'],
                      'record': str(out_path.relative_to(ROOT))}, indent=1))
    return record['passed']


def run_hook_fl(args, results):
    """The FL hook fixture (docs/architecture/weapon-effects.md 2.10): build and audit it, run it natively, write
    hook-fl-summary.json; True when it passed. The FL EXE windows are compared inside the build audit."""
    record = {'fixture': 'weapon_effects_hook_fl', 'edition': 'fl', 'bottle': None, 'game_launched': False,
              'sources': list(HOOK_FL_SOURCES), 'source': source_binding(HOOK_FL_SOURCES)}
    record['build'] = hook_build_fl.build()
    record['build']['executable_sha256'] = sha(hook_build_fl.EXE)
    done, text = native_host.run_native(hook_build_fl.EXE, [], hook_build_fl.BUILD, args.timeout,
                                        log=hook_build_fl.BUILD / 'fixture')
    report = parse_hook(text)
    record['host'] = native_host.identity(text)
    record['run'] = {'exit_code': done.returncode, 'result': report['result'],
                     'log': str((hook_build_fl.BUILD / 'fixture.log').relative_to(ROOT))}
    record['checks'] = report['checks']
    record['check_count'] = len(report['checks'])
    record['failed_checks'] = report['failed_checks']
    record['stack_depth'] = report['stack_depth']
    record['muzzle'] = report['muzzle']
    result = report['result'] or {}
    # The deepest run is the impact: the handler's audited frame plus the FL impact stub's 0x2c.
    frame = record['build']['audit']['handler_frame_bytes']
    record['stack_ok'] = bool(frame and report['stack_depth'] and report['stack_depth'].get('bytes') == frame + 0x2c)
    record['passed'] = (done.returncode == 0 and result.get('verdict') == 'PASS' and not report['failed_checks'] and
                        result.get('checks') == len(report['checks']) and result.get('failures') == 0 and
                        record['stack_ok'])
    out_path = results / 'hook-fl-summary.json'
    out_path.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'hook_fl_exit': done.returncode, 'hook_fl_result': report['result'],
                      'hook_fl_failed': report['failed_checks'], 'record': str(out_path.relative_to(ROOT))}, indent=1))
    return record['passed']


def run_fixture(args):
    if args.host == 'native':
        d3dx = native_host.windows_path(native_host.GAME_DIR / 'd3dx9_37.dll')
        done, text = native_host.run_native(EXE, [d3dx], BUILD, args.timeout, log=BUILD / 'fixture')
        return done.returncode, text
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(EXE),
               r'C:\X3\d3dx9_37.dll']
    done = subprocess.run(command, capture_output=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'),
                          timeout=args.timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    (BUILD / 'fixture.log').write_text(text)
    (BUILD / 'fixture.stderr.txt').write_bytes(done.stderr)
    return done.returncode, text


def reshade_assets(lab, target):
    """The game's IRE bolt mesh (66 vertices, x y z u v in body units) and fx_bullets_diff (512x512 RGBA) for the
    re-shade dump, from the local, untracked lab assets (tools/effects/weapon_effects_lab_assets.py, view fl) into the
    untracked build directory; False (the dump then draws its procedural stand-ins) when they are absent."""
    import numpy as np
    from PIL import Image
    script = Path(lab) / 'assets.js'
    texture = Path(lab) / 'tex' / 'fx_bullets_diff-ef82daa8.png'
    if not script.is_file() or not texture.is_file():
        return False
    text = script.read_text()
    marker = 'window.LAB_ASSETS["fl"] = '
    body = json.JSONDecoder().raw_decode(text[text.index(marker) + len(marker):])[0]['bodies'].get(
        'objects/effects/weapons/bullet_ls_alpha')
    triangles = body['materials'][0]['triangles'] if body and body.get('materials') else []
    rgba = np.asarray(Image.open(texture).convert('RGBA'))
    if len(triangles) != 22 or rgba.shape != (512, 512, 4):
        return False
    target.mkdir(parents=True, exist_ok=True)
    (target / 'ire_mesh.txt').write_text(''.join(' '.join(f'{v:.6f}' for v in vertex) + '\n'
                                                  for triangle in triangles for vertex in triangle))
    (target / 'ire_tex.rgba').write_bytes(rgba.tobytes())
    return True


def dump(args):
    """--dump DIR: the projectile look frames only (weapon_effects_projectile_dump.cpp, a separate program so the D3D
    record's source binding stays): it writes linear RGB PFM under BUILD/dump, encoded here as PNG into DIR (exposure 0,
    clamp, sRGB; no bloom, AgX or TAA, as the design mocks); no hook fixture, no record. --dump-case reshade: the bullet
    re-shade's frames instead (fixture-ire-{vanilla,reshade}-{near,far}; bullet-draw-batch.md section 6) under
    BUILD/dump-reshade, the far crops enlarged 6x (nearest) in the PNG."""
    import numpy as np
    from PIL import Image
    out = Path(args.dump)
    out.mkdir(parents=True, exist_ok=True)
    raw = BUILD / ('dump-reshade' if args.dump_case == 'reshade' else 'dump')
    raw.mkdir(parents=True, exist_ok=True)
    for stale in raw.glob('*.pfm'):
        stale.unlink()
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / 'weapon_effects_projectile_dump.exe'
    command = ['i686-w64-mingw32-g++', *FLAGS, str(ROOT / 'verification/probe/weapon_effects_projectile_dump.cpp'),
               str(ROOT / 'src/renderer/weapon_effects_pass.cpp'), '-o', str(exe), '-luser32']
    built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if built.returncode or re.search(r'\bwarning:', built.stdout + built.stderr):
        raise SystemExit('dump build failed:\n' + built.stdout + built.stderr)
    if args.dump_case == 'reshade':
        real = reshade_assets(args.lab_assets, BUILD / 'dump-reshade-assets')
        d3dx = native_host.windows_path(native_host.GAME_DIR / 'd3dx9_37.dll')
        argv = ['dump-reshade', 'reshade', d3dx, 'dump-reshade-assets' if real else '']
        if args.reshade_edge is not None:
            argv.append(f'{args.reshade_edge:g}')
    else:
        argv = ['dump']
    done, text = native_host.run_native(exe, argv, BUILD, args.timeout, log=BUILD / 'dump-fixture')
    rows = '\n'.join(line for line in text.splitlines() if line.startswith(('DUMP', 'RESULT')))
    print(rows)
    if args.dump_case == 'reshade':  # the frames' figures beside the PNGs (tracked; the assets row says game or standin)
        name = 'dump_out.txt' if args.reshade_edge is None else f'dump_e{round(args.reshade_edge * 100):03d}_out.txt'
        (out / name).write_text(rows + '\n')
    for pfm in sorted(raw.glob('*.pfm')):
        data = pfm.read_bytes()
        head = data.split(b'\n', 3)
        w, h = (int(v) for v in head[1].split())
        img = np.frombuffer(head[3], dtype='<f4', count=w * h * 3).reshape(h, w, 3)[::-1]
        c = np.clip(img, 0.0, 1.0)
        srgb = np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1 / 2.4) - 0.055)
        image = Image.fromarray((srgb * 255 + 0.5).astype(np.uint8))
        if args.dump_case == 'reshade' and '-far' in pfm.stem:
            image = image.resize((w * 6, h * 6), Image.NEAREST)
        image.save(out / (pfm.stem + '.png'))
        print('PNG', out / (pfm.stem + '.png'))
    return done.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--host', choices=('bottle', 'native'), default='bottle',
                        help='bottle (default): the CrossOver bottle X3M_FIXTURE_BOTTLE; native: the Windows side of this '
                             'WSL2 host through interop (native_host.py), never a fallback')
    parser.add_argument('--dump', help='write the projectile look frames as PNG into this directory (native host only); '
                                       'no other fixture runs and no record is written')
    parser.add_argument('--dump-case', choices=('projectile', 'reshade'), default='projectile',
                        help='with --dump: the projectile stage frames (default) or the bullet re-shade frames')
    parser.add_argument('--reshade-edge', type=float, default=None,
                        help='with --dump-case reshade: an rs_edge override; only the re-shaded frames, suffixed -eNNN')
    parser.add_argument('--lab-assets', default=str(ROOT / 'verification/results/weapon-effects-lab/fl'),
                        help='with --dump-case reshade: the untracked lab assets of the fl view (the game IRE mesh and '
                             'texture); absent, the procedural stand-ins are drawn')
    args = parser.parse_args()
    native = args.host == 'native'
    if native:
        if native_host.missing():
            raise SystemExit('--host native needs ' + '; '.join(native_host.missing()))
        if game_running() or native_host.game_running_windows():
            raise SystemExit('the game is running')
        if args.dump:
            return dump(args)
        results = native_host.results_dir(ROOT, 'weapon-effects')
    else:
        if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
            raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
        if game_running():
            raise SystemExit('the game is running')
        results = bottle.results_dir(ROOT) / 'weapon-effects'
        results.mkdir(parents=True, exist_ok=True)
    hook_passed = run_hook(args, results) if native else None  # (a): the native host only
    hook_fl_passed = run_hook_fl(args, results) if native else None  # (a): the FL hook fixture, native host only
    record = {'bottle': None if native else bottle.describe(), 'production_sources': list(PRODUCTION_SOURCES),
              'source': source_binding(), 'programs': programs_current(), 'shell_pins': SHELL_PINS, 'game_launched': False}
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
    record['metrics'] = {k: report[k] for k in ('attach', 'patch', 'symmetry', 'centre', 'ring', 'ringcells', 'fadecell',
                                                 'colour', 'relight', 'adjacent', 'placement', 'axis_plane', 'bubble_side', 'hull_walk', 'normals', 'surface', 'cull', 'sort', 'attach_slots', 'cockpit', 'grid', 'nopop', 'seam',
                                                 'stage_cpu', 'timing_maps', 'decal', 'age', 'retired', 'resolve',
                                                 'refused', 'timing', 'projectile', 'flash', 'pulse', 'pulse_cap',
                                                 'pulse_ghost', 'reshade')}
    fade = [line for line in text.splitlines() if line.startswith('FADE ')]
    record['metrics']['fade'] = fields(fade[-1]) if fade else None
    passed = bool(returncode == 0 and report['result'] and report['result']['verdict'] == 'PASS' and not report['failed_checks'])
    record['passed'] = passed
    out_path = results / 'summary.json'
    out_path.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'exit': returncode, 'result': report['result'], 'failed': report['failed_checks'],
                      'timing': report['timing'],
                      'record': str(out_path.relative_to(ROOT))}, indent=1))
    return 0 if passed and hook_passed is not False and hook_fl_passed is not False else 1


if __name__ == '__main__':
    sys.exit(main())
