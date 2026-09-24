#!/usr/bin/env python3
"""Build and audit the Terran-station LOD patch fixture. Never runs Wine.

src/proxy/terran_station_lod.cpp is compiled unchanged with the proxy's flags
plus `-include terran_lod_patch_fixture_shim.h -DX3M_TERRAN_LOD_SHIM`, which
routes its VirtualProtect, FlushInstructionCache and engine_patch
read_code/write_code calls through the fixture's pass-through fault seam;
src/proxy/engine_patch.cpp (the real lock cmpxchg8b write) is compiled as is.
The executable links at the engine's own base 0x00400000 without ASLR and
places the stub's section .x3mlod at 0x0047d000, so the window sits at the
production constant 0x0047d012 inside an image section, as in X3AP.exe. The
audit checks that the seam took (the production object imports no
VirtualProtect/FlushInstructionCache of its own and calls the seam), that
engine_patch.o carries `lock cmpxchg8b`, and that .x3mlod is one executable,
read-only page at 0x0047d000 that no other section overlaps.
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/terran-lod-patch'
EXE = BUILD / 'terran_lod_patch_fixture.exe'
SHIM = 'verification/probe/terran_lod_patch_fixture_shim.h'


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def audit(objects):
    module = tool('nm', str(objects['module']))
    imports = [l.strip() for l in module.splitlines() if re.search(r'__imp__(VirtualProtect|FlushInstructionCache)@', l)]
    seam = [name for name in ('virtual_protect', 'flush_instruction_cache', 'fixture_read_code', 'fixture_write_code') if name in module]
    listing = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    cmpxchg8b = len(re.findall(r'lock cmpxchg8b', listing))
    symbols = tool('nm', str(EXE))
    page = re.search(r'^([0-9a-f]+) T _terran_engine_page$', symbols, re.M)
    headers = tool('objdump', '-h', str(EXE)).splitlines()
    loaded = []
    for i, line in enumerate(headers):
        m = re.match(r'\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s', line)
        if m and i + 1 < len(headers):  # every section: debug sections take image VA space too
            loaded.append((m.group(1), int(m.group(3), 16), int(m.group(2), 16), headers[i + 1]))
    section = [s for s in loaded if s[0] == '.x3mlod']
    others = [s for s in loaded if s[0] != '.x3mlod']
    overlap = [s[0] for s in others if s[1] < 0x47e000 and s[1] + s[2] > 0x47d000]
    record = {'module_imports_own_protect_or_flush': imports, 'module_calls_seam': seam, 'engine_patch_lock_cmpxchg8b': cmpxchg8b,
              'engine_page_va': page.group(1) if page else None,
              'x3mlod': {'va': f'{section[0][1]:08x}', 'size': section[0][2],
                         'code_readonly': 'CODE' in section[0][3] and 'READONLY' in section[0][3]} if section else None,
              'highest_other_section_end': f'{max(s[1] + s[2] for s in others):08x}' if others else None, 'overlapping_sections': overlap}
    if (imports or len(seam) != 4 or cmpxchg8b < 1 or record['engine_page_va'] != '0047d000' or not section or section[0][2] != 4096
            or not record['x3mlod']['code_readonly'] or overlap):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    for source, stem, extra in [('verification/probe/terran_lod_patch_fixture.cpp', 'fixture', []),
                                ('src/proxy/terran_station_lod.cpp', 'module',
                                 ['-Wno-cast-function-type', '-include', str(ROOT / SHIM), '-DX3M_TERRAN_LOD_SHIM']),
                                ('src/proxy/engine_patch.cpp', 'patch', [])]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects.values()), '-static', '-static-libgcc', '-static-libstdc++',
                    '-Wl,--strip-debug', '-Wl,--image-base,0x00400000', '-Wl,--disable-dynamicbase', '-Wl,--section-start=.x3mlod=0x0047d000', '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
