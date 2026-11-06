#!/usr/bin/env python3
"""Compile the weapon effects' programs (docs/architecture/weapon-effects.md 5.1, step 1: the shield-hit hull decal, the
ship bubble and the flat decal; docs/architecture/weapon-look.md 3.1, step 2: the hull hit's sprite batch, glow and
fireball impostor): src/effects/weapon_{shell,decal,sprite,glow,fireball}_{vs,ps}.hlsl and weapon_bubble_ps.hlsl (the
bubble draws with the shell's vertex program) -> src/renderer/weapon_{shell,decal,bubble,sprite,glow,fireball}_
{vertex,pixel}_program_inc.h, provenance
verification/results/weapon-{shell,decal,bubble,sprite,glow,fireball}-{vertex,pixel}-program.json; the pixel programs include
src/effects/weapon_soft_depth.hlsl.

The compilation, validation and record are tools/shaders/generate_rigid_motion_pixel.py's (its helpers are imported:
the native d3dx9_37.dll D3DXCompileShader of the game directory through compile_rigid_motion_pixel.cpp,
D3DXSHADER_OPTIMIZATION_LEVEL3, entry main); a separate entry point keeps that tool's digest, and with it every record it
checks, unchanged (the shimmer's pattern). The records list this script among their tool sources. On a host with the
CrossOver bottle the compiler runs under its wine; on the native Windows host (WSL2, no wine) compile.exe runs directly
through WSL interop with Windows paths, after `tasklist.exe` shows no X3AP.exe / X3FL.exe. --check recompiles and
compares the checked-in headers and records without changing them. No D3D device is created.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_rigid_motion_pixel as base  # noqa: E402

ROOT = base.ROOT
PROGRAMS = {
    f'weapon_{part}_{kind}': dict(source=ROOT / f'src/effects/weapon_{part}_{kind}.hlsl',
                                  header=ROOT / f'src/renderer/weapon_{part}_{"vertex" if kind == "vs" else "pixel"}_program_inc.h',
                                  provenance=ROOT / f'verification/results/weapon-{part}-{"vertex" if kind == "vs" else "pixel"}-program.json',
                                  target=f'{kind}_3_0')
    for part, kinds in (('shell', ('vs', 'ps')), ('decal', ('vs', 'ps')), ('bubble', ('ps',)), ('sprite', ('vs', 'ps')),
                        ('glow', ('vs', 'ps')), ('fireball', ('vs', 'ps'))) for kind in kinds
}


def native_host():
    return not Path(base.bottle.WINE).exists()


def windows(path):
    return subprocess.run(['wslpath', '-w', str(path)], check=True, capture_output=True, text=True).stdout.strip()


def run_compiler(exe, compiler, source, binary, target):
    if native_host():
        subprocess.run([str(exe), windows(compiler), windows(source), windows(binary), target], check=True, timeout=300)
    else:
        subprocess.run([base.bottle.WINE, *base.bottle.wine_args(), '--dll', 'd3dx9_37=n', str(exe), 'Z:' + str(compiler),
                        'Z:' + str(source), 'Z:' + str(binary), target],
                       check=True, timeout=300, env=dict(os.environ, WINEDLLOVERRIDES='d3dx9_37=n'))


def compile_program(name, args):
    program = PROGRAMS[name]
    source, header, provenance, target = program['source'], program['header'], program['provenance'], program['target']
    expanded, included = base.expand_includes(source)
    compiler_path = args.d3dx.resolve()
    tools = (base.COMPILER_SOURCE, Path(base.__file__).resolve(), Path(__file__).resolve())
    inputs = (source, *tools, compiler_path) + tuple(included)
    before = {path: base.sha(path.read_bytes()) for path in inputs}
    with tempfile.TemporaryDirectory(prefix='x3-weapon-effects-') as directory:
        work = Path(directory)
        exe, binary = work / 'compile.exe', work / 'program.bin'
        compiled = source
        if included:
            compiled = work / source.name
            compiled.write_text(expanded)
        subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                        '-static', str(base.COMPILER_SOURCE), '-o', str(exe)], check=True)
        run_compiler(exe, compiler_path, compiled, binary, target)
        data = binary.read_bytes()
    if before != {path: base.sha(path.read_bytes()) for path in inputs}:
        raise RuntimeError('Compilation inputs changed')
    words = base.validate(data, target)
    text = '// Generated from our original %s. Do not edit.\n' % source.relative_to(ROOT)
    text += '// Reproduce: python3 tools/shaders/generate_weapon_effects_programs.py --check (native host; through wine_lock.py on a bottle)\n'
    text += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in words[i:i + 6]) + ',\n' for i in range(0, len(words), 6))
    record = dict(schema=1, source=str(source.relative_to(ROOT)), source_sha256=before[source],
                  compiler='native d3dx9_37.dll D3DXCompileShader', compiler_sha256=before[compiler_path],
                  entry='main', target=target, flags=32768, flags_name='D3DXSHADER_OPTIMIZATION_LEVEL3',
                  defines=None, includes={str(path.relative_to(ROOT)): before[path] for path in included} or None,
                  word_count=len(words), bytecode_sha256=base.sha(data), header_sha256=base.sha(text.encode()),
                  tool_sources={str(path.relative_to(ROOT)): before[path] for path in tools},
                  copyright_scope='Original authored project shader; no game shader bytes',
                  creates_d3d_device=False)
    manifest = json.dumps(record, indent=2) + '\n'
    if args.check:
        if not header.exists() or header.read_text() != text or provenance.read_text() != manifest:
            raise ValueError('%s: embedded shader/provenance differs from current native compilation' % name)
    else:
        header.write_text(text)
        provenance.write_text(manifest)
    return dict(shader=name, word_count=len(words), bytecode_sha256=base.sha(data))


def game_running():
    if native_host():
        listing = subprocess.run(['tasklist.exe'], capture_output=True, text=True, timeout=30)
        if listing.returncode != 0:
            raise RuntimeError('tasklist.exe failed')
        return [line for line in listing.stdout.splitlines() if line.lower().startswith(('x3ap.exe', 'x3fl.exe'))]
    return base.game_running()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--d3dx', type=Path, default=base.bottle.game_dir() / 'd3dx9_37.dll')
    args = parser.parse_args()
    if game_running():
        raise RuntimeError('X3AP / X3FL running or process inventory failed; postpone compilation')
    print(json.dumps(dict(result='PASS', check=args.check, shaders=[compile_program(name, args) for name in PROGRAMS])))


if __name__ == '__main__':
    main()
