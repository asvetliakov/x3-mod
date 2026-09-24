#!/usr/bin/env python3
"""Build and audit the field-of-view patch fixture. Never runs Wine.

src/proxy/fov.cpp is compiled unchanged with the proxy's flags plus
`-include fov_patch_fixture_shim.h -DX3M_FOV_SHIM`, which routes its
VirtualProtect, FlushInstructionCache and engine_patch read_code/write_code
calls through the fixture's pass-through fault seam; src/proxy/engine_patch.cpp
(the real lock cmpxchg8b write) and src/proxy/engine_memory.cpp (the validated
registry reads, its production no-SSE flags) are compiled as is. The executable
links at 0x00300000 without ASLR, so its own sections end below the engine's
pages, and places the constructor stub's section .x3mfvc at 0x0041c000 (window
at the production constant 0x0041c9cc inside an image section, as in X3AP.exe),
the reader .x3mfvr at 0x00421000, the INS_SetFocus case body .x3mfvs at
0x0042d000, the callee prefix .x3mfvx at 0x004a4000 and the writable data
section .x3mfvd at 0x00608000 (the registry slot 0x00608504, the VM slot
0x006085e4, the callee's record 0x00608600). The audit checks that the seam
took (the production object imports no VirtualProtect/FlushInstructionCache of
its own and calls the seam), that engine_patch.o carries `lock cmpxchg8b`, that
the four code sections are one
executable read-only page each and the data section one writable page at their
engine VAs, and that no other section overlaps any of them.
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
from build_cull_census import NO_SSE
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/fov-patch'
EXE = BUILD / 'fov_patch_fixture.exe'
SHIM = 'verification/probe/fov_patch_fixture_shim.h'
IMAGE_BASE = 0x300000
PAGES = {'.x3mfvc': (0x41c000, 'code'), '.x3mfvr': (0x421000, 'code'), '.x3mfvs': (0x42d000, 'code'), '.x3mfvx': (0x4a4000, 'code'),
         '.x3mfvd': (0x608000, 'data')}
SYMBOLS = {'_fov_ctor_page': '0041c000', '_fov_reader_page': '00421000', '_fov_setfocus_page': '0042d000', '_fov_callee_page': '004a4000',
           '_fov_registry_slot': '00608504', '_fov_vm_slot': '006085e4', '_fov_seen': '00608600'}


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def audit(objects):
    module = tool('nm', str(objects['module']))
    imports = [l.strip() for l in module.splitlines() if re.search(r'__imp__(VirtualProtect|FlushInstructionCache)@', l)]
    seam = [name for name in ('virtual_protect', 'flush_instruction_cache', 'fixture_read_code', 'fixture_write_code') if name in module]
    listing = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    cmpxchg8b = len(re.findall(r'lock cmpxchg8b', listing))
    symbols = tool('nm', str(EXE))
    placed = {name: (m.group(1) if (m := re.search(rf'^([0-9a-f]+) [A-Za-z] {name}$', symbols, re.M)) else None) for name in SYMBOLS}
    headers = tool('objdump', '-h', str(EXE)).splitlines()
    loaded = []
    for i, line in enumerate(headers):
        m = re.match(r'\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s', line)
        if m and i + 1 < len(headers):  # every section: debug sections take image VA space too
            loaded.append((m.group(1), int(m.group(3), 16), int(m.group(2), 16), headers[i + 1]))
    ours = {s[0]: s for s in loaded if s[0] in PAGES}
    others = [s for s in loaded if s[0] not in PAGES]
    overlap = [s[0] for s in others for va, _ in PAGES.values() if s[1] < va + 0x1000 and s[1] + s[2] > va]
    sections = {}
    for name, (va, kind) in PAGES.items():
        s = ours.get(name)
        flags = s[3] if s else ''
        ok = (s is not None and s[1] == va and s[2] == 4096 and
              (('CODE' in flags and 'READONLY' in flags) if kind == 'code' else ('DATA' in flags and 'READONLY' not in flags and 'CODE' not in flags)))
        sections[name] = {'va': f'{s[1]:08x}' if s else None, 'size': s[2] if s else None, 'ok': ok}
    record = {'module_imports_own_protect_or_flush': imports, 'module_calls_seam': seam, 'engine_patch_lock_cmpxchg8b': cmpxchg8b,
              'symbols': placed, 'sections': sections, 'image_base': f'{IMAGE_BASE:08x}',
              'highest_other_section_end': f'{max(s[1] + s[2] for s in others):08x}' if others else None, 'overlapping_sections': overlap}
    if (imports or len(seam) != 4 or cmpxchg8b < 1 or placed != SYMBOLS or not all(v['ok'] for v in sections.values()) or overlap):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    for source, stem, extra in [('verification/probe/fov_patch_fixture.cpp', 'fixture', []),
                                ('src/proxy/fov.cpp', 'module', ['-Wno-cast-function-type', '-include', str(ROOT / SHIM), '-DX3M_FOV_SHIM']),
                                ('src/proxy/engine_patch.cpp', 'patch', []),
                                ('src/proxy/engine_memory.cpp', 'memory', list(NO_SSE))]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects.values()), '-static', '-static-libgcc', '-static-libstdc++',
                    '-Wl,--strip-debug', f'-Wl,--image-base,{IMAGE_BASE:#010x}', '-Wl,--disable-dynamicbase',
                    *(f'-Wl,--section-start={name}={va:#010x}' for name, (va, _) in PAGES.items()), '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
