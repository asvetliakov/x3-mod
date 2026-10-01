#!/usr/bin/env python3
"""Build and audit the engine-effects call-redirect fixture. Never runs Wine.

src/proxy/engine_effects_patch.cpp is compiled unchanged with the proxy's flags plus
`-include engine_effects_patch_fixture_shim.h -DX3M_ENGINE_EFFECTS_SHIM`, which routes its read-back reads, its
rollback VirtualProtect and its restore_call through the fixture's one-shot fault seam; src/proxy/engine_patch.cpp
(claim_call and the real lock cmpxchg8b write) is compiled as is. The executable links at 0x00200000 without ASLR,
so its own sections end below the engine's pages, and places .x3meec at 0x00414000 (both windows at their production
VAs inside an image section, the join 0x0041487c and the effect-instance callee 0x004148a0), .x3meeb at 0x00412000
(the trail generator 0x00412d70) and the writable .x3meed at 0x00606000 (the VideoD3DFlags pointer 0x00606f34). The
audit checks that the seam took (no VirtualProtect import of the module's own, the three seam calls present), that
engine_patch.o carries `lock cmpxchg8b`, that both stubs are exactly push, mov, cmp, pop, je, jmp [continue], xor,
ret (ret 0x10 for B) with `cmp WORD PTR [eax+0x48],0x7`, that the module holds no x87/MMX opcode, that the code
sections are one executable read-only page each and the data section one writable page at their engine VAs, and that
no other section overlaps any of them.
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/engine-effects-patch'
EXE = BUILD / 'engine_effects_patch_fixture.exe'
SHIM = 'verification/probe/engine_effects_patch_fixture_shim.h'
IMAGE_BASE = 0x200000
PAGES = {'.x3meec': (0x414000, 'code'), '.x3meeb': (0x412000, 'code'), '.x3meed': (0x606000, 'data')}
SYMBOLS = {'_ee_code_page': '00414000', '_ee_window_a': '004147c4', '_ee_window_b': '004147ff', '_ee_join': '0041487c',
           '_ee_callee_a': '004148a0', '_ee_callee_b_page': '00412000', '_ee_callee_b': '00412d70', '_ee_data_page': '00606000',
           '_ee_d3d_slot': '00606f34'}
STUB = ['push', 'mov', 'cmp', 'pop', 'je', 'jmp', 'xor', 'ret']
SEAM = ('virtual_protect', 'fixture_read_code', 'fixture_restore_call')


def tool(name, *args):
    return subprocess.run(['i686-w64-mingw32-' + name, *args], capture_output=True, text=True, check=True).stdout


def stub(listing, name):
    """(mnemonics, operand text) of one stub up to its ret."""
    body = listing.split(f'<{name}>:', 1)[1].split('\n\n', 1)[0] if f'<{name}>:' in listing else ''
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z]+)\s*(.*)$', line) for line in body.splitlines()) if m]
    end = next((i for i, (m, _) in enumerate(rows) if m == 'ret'), len(rows) - 1)
    rows = rows[:end + 1]
    return [m for m, _ in rows], [o.strip() for _, o in rows]


def audit(objects):
    module = tool('nm', str(objects['module']))
    imports = [l.strip() for l in module.splitlines() if re.search(r'__imp__VirtualProtect@', l)]
    seam = [name for name in SEAM if name in module]
    patch = tool('objdump', '-d', '--no-show-raw-insn', str(objects['patch']))
    listing = tool('objdump', '-d', '-Mintel', '--no-show-raw-insn', str(objects['module']))
    a_mn, a_ops = stub(listing, '_x3m_engine_effects_stub_a')
    b_mn, b_ops = stub(listing, '_x3m_engine_effects_stub_b')
    stubs_ok = (a_mn == STUB and b_mn == STUB and a_ops[1] == 'eax,DWORD PTR [esp+0x14]' and b_ops[1] == 'eax,DWORD PTR [esp+0x10]'
                and a_ops[2] == b_ops[2] == 'WORD PTR [eax+0x48],0x7' and a_ops[7] == '' and b_ops[7] == '0x10'
                and a_ops[5].startswith('DWORD PTR ds:') and b_ops[5].startswith('DWORD PTR ds:'))
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
    record = {'module_imports_own_protect': imports, 'module_calls_seam': seam,
              'engine_patch_lock_cmpxchg8b': len(re.findall(r'lock cmpxchg8b', patch)),
              'stub_a': a_mn, 'stub_b': b_mn, 'stubs_ok': stubs_ok, 'module_x87': x87, 'symbols': found, 'pages': pages,
              'overlapping_sections': overlap, 'highest_other_section_end': f'{max(s[1] + s[2] for s in others):08x}' if others else None}
    if (imports or len(seam) != len(SEAM) or record['engine_patch_lock_cmpxchg8b'] < 1 or not stubs_ok or x87 or found != SYMBOLS
            or not all(p['ok'] for p in pages.values()) or overlap):
        raise RuntimeError('fixture build audit failed: ' + json.dumps(record))
    return record


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = {}
    for source, stem, extra in [('verification/probe/engine_effects_patch_fixture.cpp', 'fixture', []),
                                ('src/proxy/engine_effects_patch.cpp', 'module', ['-include', str(ROOT / SHIM), '-DX3M_ENGINE_EFFECTS_SHIM']),
                                ('src/proxy/engine_patch.cpp', 'patch', [])]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects[stem] = out
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects.values()), '-static', '-static-libgcc', '-static-libstdc++',
                    '-Wl,--strip-debug', f'-Wl,--image-base,{IMAGE_BASE:#010x}', '-Wl,--disable-dynamicbase', '-Wl,--disable-reloc-section',
                    *(f'-Wl,--section-start={name}={va:#010x}' for name, (va, _) in PAGES.items()), '-o', str(EXE)], check=True, cwd=ROOT)
    return {'binary': str(EXE.relative_to(ROOT)), 'toolchain': tool('g++', '--version').splitlines()[0], 'audit': audit(objects)}


if __name__ == '__main__':
    print(json.dumps(build(), indent=1))
