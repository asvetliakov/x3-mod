#!/usr/bin/env python3
"""Build and audit the run-in-background patch fixture. Never runs Wine.

src/proxy/run_in_background.cpp and src/proxy/engine_patch.cpp (the real lock cmpxchg8b write) are compiled unchanged
with the proxy's flags; verification/probe/run_in_background_patch_fixture.cpp supplies x3m::log and
object_trace::executable_verified. The executable links at 0x00200000 without ASLR, so its own sections end below the
engine's pages, and places .x3mrbc at 0x00403000 (the window at the production constant 0x00403392 inside an image
section, as in X3AP.exe), .x3mrbr at 0x004b8000 (the registry read's stand-in at 0x004b8510), .x3mrbt at 0x004d2000
(the callee's stand-in at 0x004d2580) and the writable .x3mrbd at 0x00606000 (the input block pointer 0x00606f3c). The
audit checks that engine_patch.o carries `lock cmpxchg8b`, that the module's thunk is exactly pushfd, pushad, cld, call,
popad, popfd, jmp [continue] and that the module holds no x87/MMX opcode, that the three code sections are one
executable read-only page each and the data section one writable page at their engine VAs, and that no other section
overlaps any of them.
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/run-in-background-patch'
EXE = BUILD / 'run_in_background_patch_fixture.exe'
IMAGE_BASE = 0x200000
PAGES = {'.x3mrbc': (0x403000, 'code'), '.x3mrbr': (0x4b8000, 'code'), '.x3mrbt': (0x4d2000, 'code'), '.x3mrbd': (0x606000, 'data')}
SYMBOLS = {'_rib_code_page': '00403000', '_rib_window': '00403392', '_rib_registry_page': '004b8000', '_rib_callee_page': '004d2000',
           '_rib_data_page': '00606000', '_rib_slot': '00606f3c'}
THUNK = ['pushf', 'pusha', 'cld', 'call', 'popa', 'popf', 'jmp']


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def audit(objects):
    patch = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    listing = tool('objdump', '-d', '-Mintel', '--no-show-raw-insn', str(objects['module']))
    body = listing.split('<_x3m_run_in_background_thunk>:', 1)[1].split('\n\n', 1)[0] if '<_x3m_run_in_background_thunk>:' in listing else ''
    thunk = [m.group(1) for m in re.finditer(r'^\s*[0-9a-f]+:\s+([a-z]+)', body, re.M)]
    jump = re.search(r'jmp\s+DWORD PTR ds:0x0', body) is not None
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\s*(.*)$', line) for line in listing.splitlines()) if m]
    x87 = [m for m, o in rows if re.match(r'^f[a-z0-9]+$', m) or m == 'emms' or re.search(r'\b(st\(\d\)|mm[0-7])\b', o)]
    symbols = tool('nm', str(EXE))
    found = {name: (re.search(rf'^([0-9a-f]+) [A-Za-z] {re.escape(name)}$', symbols, re.M) or [None, None])[1] for name in SYMBOLS}
    headers = tool('objdump', '-h', str(EXE)).splitlines()
    loaded = []
    for i, line in enumerate(headers):
        m = re.match(r'\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s', line)
        if m and i + 1 < len(headers):
            loaded.append((m.group(1), int(m.group(3), 16), int(m.group(2), 16), headers[i + 1]))
    pages = {}
    for name, (va, kind) in PAGES.items():
        section = [s for s in loaded if s[0] == name]
        flags = section[0][3] if section else ''
        ok = bool(section) and section[0][1] == va and section[0][2] == 4096 and (
            ('CODE' in flags and 'READONLY' in flags) if kind == 'code' else ('DATA' in flags and 'READONLY' not in flags and 'CODE' not in flags))
        pages[name] = {'va': f'{section[0][1]:08x}' if section else None, 'size': section[0][2] if section else None, 'ok': ok}
    others = [s for s in loaded if s[0] not in PAGES]
    overlap = [s[0] for s in others for va, _ in PAGES.values() if s[1] < va + 0x1000 and s[1] + s[2] > va]
    record = {'engine_patch_lock_cmpxchg8b': len(re.findall(r'lock cmpxchg8b', patch)), 'thunk': thunk, 'thunk_jump_indirect': jump,
              'module_x87': x87, 'symbols': found, 'pages': pages, 'overlapping_sections': overlap,
              'highest_other_section_end': f'{max(s[1] + s[2] for s in others):08x}' if others else None}
    if (record['engine_patch_lock_cmpxchg8b'] < 1 or thunk != THUNK or not jump or x87 or found != SYMBOLS
            or not all(p['ok'] for p in pages.values()) or overlap):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    for source, stem in [('verification/probe/run_in_background_patch_fixture.cpp', 'fixture'),
                         ('src/proxy/run_in_background.cpp', 'module'), ('src/proxy/engine_patch.cpp', 'patch')]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects.values()), '-static', '-static-libgcc', '-static-libstdc++',
                    '-Wl,--strip-debug', f'-Wl,--image-base,{IMAGE_BASE:#010x}', '-Wl,--disable-dynamicbase', '-Wl,--disable-reloc-section',
                    *(f'-Wl,--section-start={name}={va:#010x}' for name, (va, _) in PAGES.items()), '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
