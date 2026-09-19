#!/usr/bin/env python3
"""Build and audit the collide-narrow-census CPU fixture. Never runs Wine.

The three stubs are encoded with the Python twins of the C++ encoders,
disassembled and audited. Sites 6 and 7: `inc dword [abs]`, the displaced
`mov eax,[hits]` (site 7) and one `jmp`, nothing else. Site 5: the exact
instruction sequence, in which every handler call sits inside a
pushfd/pushad/XMM0-7 ... popad/popfd bracket and nothing but such a bracket
and one `mov byte` runs between the engine callee's return and the jump back,
so EAX, EFLAGS and every register reach the engine as the callee left them;
no x87/MMX instruction in any stub nor in the production module's object.
"""
import json
import re
import subprocess
import tempfile
from pathlib import Path
from run_chase_aim_trace import FLAGS
import verify_collide_sites as sites
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/collide-narrow-census'
OBJDUMP = 'i686-w64-mingw32-objdump'
SAVE = ['pushf', 'pusha', 'cld', 'sub'] + ['movups'] * 8
RESTORE = ['movups'] * 8 + ['add', 'popa', 'popf']
N5_SEQUENCE = (['cmp', 'jne', 'cmp', 'jne', 'mov'] + SAVE + ['push', 'push', 'call', 'add'] + RESTORE + ['lea', 'call']
               + SAVE + ['push', 'call', 'add'] + RESTORE + ['mov', 'jmp', 'inc', 'jmp', 'inc', 'jmp'])


def mnemonics(code):
    with tempfile.NamedTemporaryFile(suffix='.bin') as f:
        f.write(code)
        f.flush()
        listing = subprocess.run([OBJDUMP, '-D', '-b', 'binary', '-mi386', '-Mintel', '--no-show-raw-insn', f.name],
                                 capture_output=True, text=True, check=True).stdout
    return [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z(][a-z0-9)]*)\s*(.*)$', line) for line in listing.splitlines()) if m]


def audit_stubs():
    n5 = mnemonics(sites.encode_n5_stub(0x10000000, sites.N5_RETURN, sites.N5_TARGET, 0x20001000, 0x20002000, 0x20000000, 0x20000008, 0x2000000c))
    n6 = mnemonics(sites.encode_n6_stub(0x10000200, 0x20000010, sites.N6_TARGET))
    n7 = mnemonics(sites.encode_n7_stub(0x10000300, 0x20000014, sites.N7_HITS, sites.N7_NEXT))
    names = [re.sub(r'^(pushf|popf|pusha|popa)[dw]$', r'\1', m) for m, _ in n5]
    if names != N5_SEQUENCE:
        raise RuntimeError(f'site-5 stub: unexpected instruction sequence {names}')
    if any(m.startswith('f') or m == 'emms' or re.search(r'\b(mm|st)\d?\b', o) for m, o in n5 + n6 + n7):
        raise RuntimeError('x87/MMX instruction in a stub')
    if [m for m, _ in n6] != ['inc', 'jmp'] or [m for m, _ in n7] != ['inc', 'mov', 'jmp'] or not n7[1][1].replace(' ', '').startswith('eax,'):
        raise RuntimeError(f'site-6/7 stub: unexpected instructions {n6} {n7}')
    return {'n5': {'instructions': len(n5), 'sequence_exact': True, 'bracketed_handler_calls': 2}, 'n6': [m for m, _ in n6], 'n7': [m for m, _ in n7], 'no_x87_mmx': True}


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    audit = audit_stubs()
    objects = []
    for source, stem in [('verification/probe/collide_narrow_census_fixture.cpp', 'fixture'), ('src/proxy/collide_narrow_census.cpp', 'module'),
                         ('src/proxy/engine_patch.cpp', 'patch')]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects.append(out)
    exe = BUILD / 'collide_narrow_census_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects), '-static', '-static-libgcc', '-static-libstdc++', '-o', str(exe)], check=True, cwd=ROOT)
    # The production module's object: no x87 opcode anywhere in it (its handlers run around the engine's x87 narrow phase).
    listing = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', str(BUILD / 'module.o')], capture_output=True, text=True, check=True).stdout
    x87 = [line.strip() for line in listing.splitlines() if re.match(r'^\s*[0-9a-f]+:\s+(f[a-z0-9]+|emms)\b', line)]
    if x87:
        raise RuntimeError('x87 in the production module: ' + '; '.join(x87[:4]))
    return {'binary': str(exe), 'stub_audit': audit, 'module_x87_instructions': 0, 'runtime': 'not run'}


if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
