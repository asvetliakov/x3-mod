#!/usr/bin/env python3
"""FL sun occlusion hook CPU-state audit (docs/architecture/farnhams-legacy-sun-occlusion.md).

Walks the static direct-call graph of a built image (the fl d3d9.dll or the native fixture) from the probe thunk and
its C handler with verification/probe/check_no_x87.py's walker and reports, per reachable function, any x87 opcode
outside check_no_x87's state-transport set and any XMM/MMX register operand other than the XmmSave envelope (`movups`
of XMM0-7 out to memory and back, sixteen per envelope). The log formatter and the original probe (log mode) are
reached through indirect calls, by design: they run inside the envelope. Required transport: one XMM envelope in the
handler and no x87/MXCSR transport (integer only). The lens thunk and its begin/end handlers must be absent (the lens
bracket was deleted on 2026-11-02). The walk stops at x3m::log (the session log: SSE code with call_preserved around
the formatter, session_log.cpp); it is called only from inside the handler's envelope (the log-mode sun_probe row),
and the callers that reach it are listed under `log_callers`.

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
THUNKS = ['_x3m_fl_sun_probe_thunk']
HANDLERS = ['_x3m_fl_sun_probe_decide']
GONE = ['_x3m_fl_sun_lens_thunk', '_x3m_fl_sun_lens_begin', '_x3m_fl_sun_lens_end']
REGISTER = re.compile(r'%(?:x|y)mm\d|%mm\d')
MOVE = re.compile(r'\tmovups\s+(?:%xmm[0-7],[^%]*\(|[^%]*\([^)]*\),%xmm[0-7]$)')
TRANSPORT = ('fnsave', 'fninit', 'frstor', 'stmxcsr', 'ldmxcsr')
STOP = '__ZN3x3m3logEPKcz'


def walk(functions, roots):
    """check_no_x87.walk without the edges into STOP; returns (seen, callers of STOP)."""
    pruned = {n: [l for l in body if f'<{STOP}>' not in l] for n, body in functions.items()}
    callers = sorted(n for n, body in functions.items() if any(f'<{STOP}>' in l for l in body))
    seen = x87.walk(pruned, roots)
    return seen, [c for c in callers if c in seen]


def main():
    functions = x87.disassemble(Path(sys.argv[1]))
    roots = THUNKS + HANDLERS
    missing = [r for r in roots if r not in functions]
    seen, log_callers = walk(functions, roots)
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
    transport = {h: {m: sum(1 for l in functions.get(h, []) if x87.parse(l)[0] == m) for m in TRANSPORT}
                 for h in HANDLERS}
    transport_ok = (all(envelopes.get(h) == 16 for h in HANDLERS) and
                    all(not any(transport[h].values()) for h in HANDLERS) and
                    not any(envelopes.get(t) for t in THUNKS))
    present = [g for g in GONE if g in functions]
    out = {'roots': roots, 'missing_roots': missing, 'reachable': len(seen), 'x87': x87_hits, 'simd': simd,
           'envelopes': envelopes, 'transport': transport, 'transport_ok': transport_ok, 'log_callers': log_callers,
           'lens_symbols_present': present}
    print(json.dumps(out))
    return 1 if missing or x87_hits or simd or not transport_ok or present else 0


if __name__ == '__main__':
    sys.exit(main())
