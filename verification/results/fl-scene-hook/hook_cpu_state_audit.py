#!/usr/bin/env python3
"""FL scene-end hook CPU-state audit (docs/architecture/farnhams-legacy-scene.md).

Walks the static direct-call graph of a built image (the fl d3d9.dll or the native fixture) from the signal
trampoline and its signal function (_x3m_fl_scene_trampoline, _x3m_fl_scene_signal) with
verification/probe/check_no_x87.py's walker and reports, per reachable function, any x87 opcode outside
check_no_x87's state-transport set (fnsave/fninit/frstor, stmxcsr/ldmxcsr) and any XMM/MMX register operand other
than the XmmSave envelope (`movups` of XMM0-7 out to memory and back, sixteen per envelope). The listener (capture's
scene_end_signal, SSE code) is reached through an indirect call, by design: it runs inside both envelopes. The
transport itself must be present in the signal: one fnsave, one fninit, one frstor and one XMM envelope.

usage: hook_cpu_state_audit.py <image>   (exit 1 on a finding; prints one JSON line)
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
ROOTS = ['_x3m_fl_scene_trampoline', '_x3m_fl_scene_signal']
REGISTER = re.compile(r'%(?:x|y)mm\d|%mm\d')
MOVE = re.compile(r'\tmovups\s+(?:%xmm[0-7],[^%]*\(|[^%]*\([^)]*\),%xmm[0-7]$)')


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
    signal = functions.get('_x3m_fl_scene_signal', [])
    transport = {m: sum(1 for l in signal if x87.parse(l)[0] == m) for m in ('fnsave', 'fninit', 'frstor', 'stmxcsr',
                                                                              'ldmxcsr')}
    transport_ok = all(transport[m] == 1 for m in transport) and envelopes.get('_x3m_fl_scene_signal') == 16
    out = {'roots': ROOTS, 'missing_roots': missing, 'reachable': len(seen), 'x87': x87_hits, 'simd': simd,
           'envelopes': envelopes, 'signal_transport': transport, 'transport_ok': transport_ok}
    print(json.dumps(out))
    return 1 if missing or x87_hits or simd or not transport_ok else 0


if __name__ == '__main__':
    sys.exit(main())
