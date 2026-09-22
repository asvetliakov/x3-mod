#!/usr/bin/env python3
"""Build and audit the cull-census CPU fixture. Never runs Wine.

The production module is compiled with its production flags (no SSE/MMX, x87
fpmath with no floating point, no exceptions) and the object is audited: no
x87, MMX or XMM instruction anywhere in it and no exception-runtime symbol.
"""
import json
import re
import subprocess
from pathlib import Path
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/cull-census'
NO_SSE = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
ALLOWED = {'fwait'}
FIXTURE_DEFINE = '-DX3M_CULL_CENSUS_FIXTURE'
HANDLERS = ('_x3m_cull_census_measure', '_x3m_cull_census_exit')


def audit_module(path):
    symbols = subprocess.run(['i686-w64-mingw32-objdump', '-t', str(path)], capture_output=True, text=True, check=True).stdout
    forbidden = [line for line in symbols.splitlines() if any(name in line for name in ('_Unwind_', '__gxx_personality', '__cxa_throw'))]
    if forbidden:
        raise RuntimeError('exception runtime symbols in the handler module: ' + '; '.join(forbidden))
    listing = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', '-w', str(path)], capture_output=True, text=True, check=True).stdout
    violations = []
    functions = 0
    for line in listing.splitlines():
        if re.match(r'^[0-9a-f]+ <', line):
            functions += 1
        m = re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\s*(.*)$', line)
        if not m:
            continue
        mnemonic, operands = m.groups()
        if (mnemonic.startswith('f') and mnemonic not in ALLOWED) or re.search(r'\b(xmm|mm)\d', operands):
            violations.append(line.strip())
    if violations:
        raise RuntimeError('x87/MMX/XMM instruction in the handler module: ' + '; '.join(violations[:4]))
    missing = [h for h in HANDLERS if h not in listing]
    if missing:
        raise RuntimeError('handler symbol missing: ' + ', '.join(missing))
    return {'functions': functions, 'no_x87_mmx_xmm': True, 'no_exception_runtime_symbols': True, 'handlers': list(HANDLERS)}


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = []
    # X3M_CULL_CENSUS_FIXTURE: the body-table global seam (set_body_table_global), absent from production.
    for source, stem, extra in [('verification/probe/cull_census_fixture.cpp', 'fixture', [FIXTURE_DEFINE]),
                                ('src/proxy/cull_census.cpp', 'module', [*NO_SSE, FIXTURE_DEFINE]),
                                ('src/proxy/engine_patch.cpp', 'patch', []),
                                ('src/proxy/engine_memory.cpp', 'memory', NO_SSE)]:
        out = BUILD / (stem + '.o')
        subprocess.run(['i686-w64-mingw32-g++', *FLAGS, *extra, '-c', str(ROOT / source), '-o', str(out)], check=True, cwd=ROOT)
        objects.append(out)
    audit = audit_module(BUILD / 'module.o')
    exe = BUILD / 'cull_census_fixture.exe'
    subprocess.run(['i686-w64-mingw32-g++', *map(str, objects), '-static', '-static-libgcc', '-static-libstdc++', '-o', str(exe)], check=True, cwd=ROOT)
    return {'binary': str(exe), 'module_audit': audit, 'runtime': 'not run'}


if __name__ == '__main__':
    print(json.dumps(build(), indent=2))
