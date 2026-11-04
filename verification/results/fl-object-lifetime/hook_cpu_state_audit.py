#!/usr/bin/env python3
"""FL registry observer CPU-state audit (docs/architecture/farnhams-legacy-lifetime.md).

Walks the static direct-call graph of a built image (the fl d3d9.dll or the fixture exe) from the four stubs, the
dispatcher and its three helpers (_x3m_fl_lifetime_insert/remove/destroy/load, _dispatch, _enter, _leave, _unwind)
with verification/probe/check_no_x87.py's walker and reports, per reachable function, any x87 opcode outside
check_no_x87's state-transport set and any XMM/MMX register operand. The FXSAVE/FXRSTOR envelope is expected in
exactly two places: the dispatcher (two FXSAVE, two FXRSTOR: around enter() and around leave()) and the unwind
handler (one of each); anywhere else it is a finding. The walk also enters engine_memory::read's cold C++ unwind
path (_Unwind_Resume, libgcc, winpthreads), never taken on a read; those functions are audited like the rest.

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
ROOTS = ['_x3m_fl_lifetime_insert', '_x3m_fl_lifetime_remove', '_x3m_fl_lifetime_destroy', '_x3m_fl_lifetime_load',
         '_x3m_fl_lifetime_dispatch', '_x3m_fl_lifetime_enter', '_x3m_fl_lifetime_leave', '_x3m_fl_lifetime_unwind']
ENVELOPE = {'_x3m_fl_lifetime_dispatch': {'fxsave': 2, 'fxrstor': 2},
            '_x3m_fl_lifetime_unwind': {'fxsave': 1, 'fxrstor': 1}}
REGISTER = re.compile(r'%(?:x|y)mm\d|%mm\d')


def main():
    functions = x87.disassemble(Path(sys.argv[1]))
    missing = [r for r in ROOTS if r not in functions]
    seen = x87.walk(functions, ROOTS)
    findings, envelopes = {}, {}
    for name, hits in seen.items():
        counts = {}
        rest = []
        # Count the envelope from the function's own listing: check_no_x87 allows fxsave/fxrstor as
        # state transport since 2026-11-04, so they no longer appear among its hits.
        for line in functions[name]:
            mnemonic = x87.parse(line)[0]
            if mnemonic in ('fxsave', 'fxrstor'):
                counts[mnemonic] = counts.get(mnemonic, 0) + 1
        rest.extend(line for line in hits if x87.parse(line)[0] not in ('fxsave', 'fxrstor'))
        if counts:
            envelopes[name] = counts
            if ENVELOPE.get(name) != counts:
                rest.append(f'unexpected FXSAVE/FXRSTOR {counts}')
        rest += [line.strip() for line in functions[name] if REGISTER.search(line)]
        if rest:
            findings[name] = rest[:4]
    wrong = {k: v for k, v in ENVELOPE.items() if envelopes.get(k) != v}
    out = {'roots': ROOTS, 'missing_roots': missing, 'reachable': len(seen), 'findings': findings,
           'envelopes': envelopes, 'envelope_mismatch': wrong}
    print(json.dumps(out))
    return 1 if missing or findings or wrong else 0


if __name__ == '__main__':
    sys.exit(main())
