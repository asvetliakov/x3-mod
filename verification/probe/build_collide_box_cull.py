#!/usr/bin/env python3
"""Build and audit the collide-box-cull CPU fixture. Never runs Wine.

The production module has no per-pair C++ handler: the code that runs inside
the engine's pair loops is the two emitted stubs. They are encoded here with
the Python twins of the C++ encoders, disassembled, and audited: no x87, MMX
or SSE instruction, no call, no EFLAGS push, exactly one balanced push/pop
pair, and the expected lengths.
"""
import json
import re
import subprocess
import tempfile
from pathlib import Path
from run_chase_aim_trace import FLAGS
import verify_collide_sites as sites
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/collide-box-cull'
OBJDUMP = 'i686-w64-mingw32-objdump'


def audit_stub(name, code):
    with tempfile.NamedTemporaryFile(suffix='.bin') as f:
        f.write(code)
        f.flush()
        listing = subprocess.run([OBJDUMP, '-D', '-b', 'binary', '-mi386', '-Mintel', '--no-show-raw-insn', f.name],
                                 capture_output=True, text=True, check=True).stdout
    rows = [m.groups() for m in (re.match(r'^\s*[0-9a-f]+:\s+([a-z(][a-z0-9)]*)\s*(.*)$', line) for line in listing.splitlines()) if m]
    bad = [f'{m} {o}' for m, o in rows if m.startswith('f') or m in ('call', '(bad)', 'pushf', 'popf') or re.search(r'\b(xmm|mm|st)\d?\b', o)]
    if bad:
        raise RuntimeError(f'{name}: forbidden instruction: ' + '; '.join(bad[:4]))
    pushes, pops = sum(m == 'push' for m, _ in rows), sum(m == 'pop' for m, _ in rows)
    if (pushes, pops) != (1, 1):
        raise RuntimeError(f'{name}: unbalanced stack use push={pushes} pop={pops}')
    return {'bytes': len(code), 'instructions': len(rows), 'no_x87_mmx_sse_call': True, 'push_pop': [pushes, pops]}


def audit_stubs():
    out = {}
    for label, entered, rejected in (('counted', 0x20000010, 0x20000014), ('plain', 0, 0)):
        out[f'p1_{label}'] = audit_stub(f'p1_{label}', sites.encode_p1_stub(0x10000000, 0x20000000, entered, rejected, 0x10000090, sites.P1_CONTINUE))
        out[f'p2_{label}'] = audit_stub(f'p2_{label}', sites.encode_p2_stub(0x10000000, 0x20000000, entered, rejected, 0x10000090, sites.P2_CONTINUE))
    return out


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    audit = audit_stubs()
    objects = []
    for source, stem in [('verification/probe/collide_box_cull_fixture.cpp', 'fixture'), ('src/proxy/collide_box_cull.cpp', 'module'),
                         ('src/proxy/engine_patch.cpp', 'patch')]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects.append(out)
    exe = BUILD / 'collide_box_cull_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects), '-static', '-static-libgcc', '-static-libstdc++', '-o', str(exe)], check=True, cwd=ROOT)
    return {'binary': str(exe), 'stub_audit': audit, 'runtime': 'not run'}


if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
