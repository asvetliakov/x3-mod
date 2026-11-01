#!/usr/bin/env python3
"""FL scaler replacement CPU-state audit (docs/architecture/farnhams-legacy-scale-overflow.md).

Walks the static direct-call graph of a built image (the fl d3d9.dll or the fixture exe) from the entry stub and
its body (_x3m_fl_scale_entry, _x3m_fl_scale_body) with verification/probe/check_no_x87.py's walker and reports,
per reachable function, any x87 opcode outside check_no_x87's state-transport set and any XMM/MMX register
operand other than the QPC/row path's XmmSave envelope (movups of XMM0-7 out and back, sixteen per envelope).
log() is reached only through call_preserved's indirect thunk, which the walk does not follow, by design.
It also lists the per-call path: the instructions of _x3m_fl_scale_body and _x3m_fl_scale_entry must contain no
x87 and no XMM operand at all, and no call other than the rare check_sample (no libgcc __divdi3/__muldi3).

usage: cpu_state_audit.py build-fl/d3d9.dll   (exit 1 on a finding; prints one JSON line)
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
ROOTS = ['_x3m_fl_scale_entry', '_x3m_fl_scale_body']
REGISTER = re.compile(r'%(?:x|y)mm\d|%mm\d')
MOVE = re.compile(r'\tmovups\s+(?:%xmm[0-7],[^%]*\(|[^%]*\([^)]*\),%xmm[0-7]$)')
X87 = re.compile(r'\tf[a-z0-9]+\s')
CALL = re.compile(r'\tcall\s+[0-9a-f]+ <([^>]+)>')


def main():
    functions = x87.disassemble(Path(sys.argv[1]))
    missing = [r for r in ROOTS if r not in functions]
    seen = x87.walk(functions, ROOTS)
    x87_hits = {k: v for k, v in seen.items() if v}
    simd, envelopes = {}, {}
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
    hot = {}
    for name in ROOTS:
        body = functions.get(name, [])
        hot[name] = {'instructions': len(body),
                     'x87_or_simd': [l.strip() for l in body if REGISTER.search(l) or X87.search(l)][:4],
                     'calls': sorted({m.group(1) for l in body for m in [CALL.search(l)] if m})}
    bad_calls = [c for c in hot.get('_x3m_fl_scale_body', {}).get('calls', []) if 'check_sample' not in c]
    out = {'roots': ROOTS, 'missing_roots': missing, 'reachable': len(seen), 'x87': x87_hits, 'simd': simd,
           'envelopes': envelopes, 'hot': hot, 'hot_bad_calls': bad_calls}
    print(json.dumps(out))
    hot_dirty = any(v['x87_or_simd'] for v in hot.values())
    return 1 if missing or x87_hits or simd or hot_dirty or bad_calls else 0


if __name__ == '__main__':
    sys.exit(main())
