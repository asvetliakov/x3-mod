#!/usr/bin/env python3
"""Build and audit the LOD occlusion patch fixture. Never runs Wine.

src/proxy/lod_occlusion.cpp is compiled unchanged with the proxy's flags plus
`-include lod_occlusion_patch_fixture_shim.h -DX3M_LOD_OCCLUSION_SHIM`, which
routes its VirtualProtect, FlushInstructionCache and engine_patch
read_code/write_code calls through the fixture's pass-through fault seam;
src/proxy/engine_patch.cpp (the real lock cmpxchg8b write) is compiled as is.
The executable links at the engine's own base 0x00400000 without ASLR, places
the stub's section .x3mocc at 0x004c3000, so the window sits at the production
constant 0x004c34e7 inside an image section, as in X3AP.exe, and the section
.x3mocd at 0x00606000, so the window's absolute `mov edx,[00606f74]` reads a
mapped marker. The audit checks that the seam took (the production object
imports no VirtualProtect/FlushInstructionCache of its own and calls the
seam), that engine_patch.o carries `lock cmpxchg8b`, that .x3mocc is one
executable, read-only page at 0x004c3000 and .x3mocd one read-only data page
at 0x00606000, and that no other section overlaps either page.
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/lod-occlusion-patch'
EXE = BUILD / 'lod_occlusion_patch_fixture.exe'
SHIM = 'verification/probe/lod_occlusion_patch_fixture_shim.h'
CODE_PAGE, DATA_PAGE = 0x4c3000, 0x606000


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def audit(objects):
    module = tool('nm', str(objects['module']))
    imports = [l.strip() for l in module.splitlines() if re.search(r'__imp__(VirtualProtect|FlushInstructionCache)@', l)]
    seam = [name for name in ('virtual_protect', 'flush_instruction_cache', 'fixture_read_code', 'fixture_write_code') if name in module]
    listing = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    cmpxchg8b = len(re.findall(r'lock cmpxchg8b', listing))
    symbols = tool('nm', str(EXE))
    page = re.search(r'^([0-9a-f]+) T _occl_engine_page$', symbols, re.M)
    marker = re.search(r'^([0-9a-f]+) [A-Za-z] _occl_placeholder_global$', symbols, re.M)
    headers = tool('objdump', '-h', str(EXE)).splitlines()
    loaded = []
    for i, line in enumerate(headers):
        m = re.match(r'\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s', line)
        if m and i + 1 < len(headers):  # every section: debug sections take image VA space too
            loaded.append((m.group(1), int(m.group(3), 16), int(m.group(2), 16), headers[i + 1]))
    code = [s for s in loaded if s[0] == '.x3mocc']
    data = [s for s in loaded if s[0] == '.x3mocd']
    others = [s for s in loaded if s[0] not in ('.x3mocc', '.x3mocd')]
    overlap = [s[0] for s in others for lo in (CODE_PAGE, DATA_PAGE) if s[1] < lo + 0x1000 and s[1] + s[2] > lo]
    record = {'module_imports_own_protect_or_flush': imports, 'module_calls_seam': seam, 'engine_patch_lock_cmpxchg8b': cmpxchg8b,
              'engine_page_va': page.group(1) if page else None, 'placeholder_global_va': marker.group(1) if marker else None,
              'x3mocc': {'va': f'{code[0][1]:08x}', 'size': code[0][2],
                         'code_readonly': 'CODE' in code[0][3] and 'READONLY' in code[0][3]} if code else None,
              'x3mocd': {'va': f'{data[0][1]:08x}', 'size': data[0][2],
                         'data_readonly': 'DATA' in data[0][3] and 'READONLY' in data[0][3] and 'CODE' not in data[0][3]} if data else None,
              'highest_other_section_end': f'{max(s[1] + s[2] for s in others):08x}' if others else None, 'overlapping_sections': overlap}
    if (imports or len(seam) != 4 or cmpxchg8b < 1 or record['engine_page_va'] != '004c3000' or record['placeholder_global_va'] != '00606f74'
            or not code or code[0][2] != 4096 or not record['x3mocc']['code_readonly']
            or not data or data[0][2] != 4096 or not record['x3mocd']['data_readonly'] or overlap):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    for source, stem, extra in [('verification/probe/lod_occlusion_patch_fixture.cpp', 'fixture', []),
                                ('src/proxy/lod_occlusion.cpp', 'module',
                                 ['-Wno-cast-function-type', '-include', str(ROOT / SHIM), '-DX3M_LOD_OCCLUSION_SHIM']),
                                ('src/proxy/engine_patch.cpp', 'patch', [])]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects.values()), '-static', '-static-libgcc', '-static-libstdc++',
                    '-Wl,--strip-debug', '-Wl,--image-base,0x00400000', '-Wl,--disable-dynamicbase',
                    f'-Wl,--section-start=.x3mocc={CODE_PAGE:#010x}', f'-Wl,--section-start=.x3mocd={DATA_PAGE:#010x}', '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
