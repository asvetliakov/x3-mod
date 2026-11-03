#!/usr/bin/env python3
"""Static walk of the FL oversized-disc C side in a built FL proxy DLL (default build-fl/d3d9.dll).

Roots: _x3m_circle_fill_fl_wide (entered from the pushfd/pushad stub, which saves no XMM or x87 state). Walks direct
calls with verification/probe/check_no_x87.py's walker, stopping at x3m::log (called once, inside the FXSAVE/FXRSTOR
envelope of the first-wide row). Reports every x87 instruction (outside fxsave/fxrstor) and every XMM-touching
instruction in the walked functions; the only expected XMM user is the asm B conversion _x3m_circle_fill_fl_axis
(the engine's own sequence).

usage: python3 verification/results/fl-circle-fill/walk_wide.py [dll]
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import check_no_x87 as cnx  # noqa: E402


def main():
    dll = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build-fl/d3d9.dll'
    functions = cnx.disassemble(dll)
    root = '_x3m_circle_fill_fl_wide'
    if root not in functions:
        print(json.dumps({'result': 'FAIL', 'error': f'{root} not found'}))
        return 1
    log = [n for n in functions if n.startswith('__ZN3x3m3logEPKcz')]
    stop = set(log)
    seen, stack = {}, [root]
    while stack:
        name = stack.pop()
        if name in seen or name not in functions or name in stop:
            continue
        x87, xmm = [], []
        for line in functions[name]:
            mnemonic, operands = cnx.parse(line)
            if not mnemonic:
                continue
            if mnemonic.startswith('f') and mnemonic not in ('fxsave', 'fxrstor'):
                x87.append(line.strip())
            if 'xmm' in operands or 'mxcsr' in mnemonic:
                xmm.append(line.strip())
            if mnemonic in ('call', 'jmp'):
                m = re.match(r'^[0-9a-f]+ <([^>]+)>', operands)
                if m and not m.group(1).startswith('__imp_'):
                    stack.append(re.sub(r'[+-]0x[0-9a-f]+$', '', m.group(1)))
        seen[name] = {'x87': x87, 'xmm': len(xmm)}
    xmm_users = sorted(n for n, v in seen.items() if v['xmm'])
    x87_users = sorted(n for n, v in seen.items() if v['x87'])
    result = 'PASS' if not x87_users and xmm_users == ['_x3m_circle_fill_fl_axis'] else 'FAIL'
    print(json.dumps({'result': result, 'dll': str(dll), 'walked': sorted(seen), 'stopped_at': sorted(stop),
                      'x87_users': x87_users, 'xmm_users': xmm_users}, indent=1))
    return 0 if result == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
