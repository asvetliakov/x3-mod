#!/usr/bin/env python3
"""FL scope hook CPU-state audit (docs/architecture/farnhams-legacy-object-identity.md, "Step 3a").

Walks the static direct-call graph of the built fl d3d9.dll from the dispatcher and its three helpers
(_x3m_fl_object_dispatch/_enter/_leave/_unwind) with verification/probe/check_no_x87.py's walker and
reports, per reachable function, any x87 opcode outside check_no_x87's state-transport set and any
XMM/MMX register operand other than the periodic row's `preserved<>` envelope: `movups` of XMM0-7 out to
memory and back, sixteen per envelope (it may be inlined into write_sample). The logger behind it is
reached only through call_preserved's indirect thunk, which the walk does not follow, by design. The walk
also enters engine_memory::read's cold C++ unwind path (_Unwind_Resume, libgcc, winpthreads), never taken
on a read; those functions are audited like the rest.

usage: hook_cpu_state_audit.py build-fl/d3d9.dll   (exit 1 on a finding; prints one JSON line)
"""
import importlib.util
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('check_no_x87', ROOT / 'verification/probe/check_no_x87.py')
x87 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x87)
ROOTS = ['_x3m_fl_object_dispatch', '_x3m_fl_object_enter', '_x3m_fl_object_leave', '_x3m_fl_object_unwind']
REGISTER = re.compile(r'%(?:x|y)mm\d|%mm\d')
MOVE = re.compile(r'\tmovups\s+(?:%xmm[0-7],[^%]*\(|[^%]*\([^)]*\),%xmm[0-7]$)')


def main():
    functions = x87.disassemble(Path(sys.argv[1]))
    missing = [r for r in ROOTS if r not in functions]
    seen = x87.walk(functions, ROOTS)
    x87_hits = {k: v for k, v in seen.items() if v}
    simd = {}
    envelopes = {}
    for name in seen:
        lines = [line.strip() for line in functions[name] if REGISTER.search(line)]
        moves = [l for l in lines if MOVE.search(l)]
        hits = [l for l in lines if not MOVE.search(l)]
        if moves:
            envelopes[name] = len(moves)
            if len(moves) % 16:
                hits.append(f'{len(moves)} xmm0-7 movups (not whole save/restore pairs)')
        if hits:
            simd[name] = hits[:4]
    out = {'roots': ROOTS, 'missing_roots': missing, 'reachable': len(seen), 'x87': x87_hits, 'simd': simd,
           'envelopes': envelopes}
    print(json.dumps(out))
    return 1 if missing or x87_hits or simd else 0


if __name__ == '__main__':
    sys.exit(main())
