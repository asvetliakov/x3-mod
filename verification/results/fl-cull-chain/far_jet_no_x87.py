#!/usr/bin/env python3
"""The far-jet handler in the fl DLL (src/proxy/engine_far_jets.cpp, FL model offset +0x144): check_no_x87.py's walk from
`_x3m_engine_far_jet` alone (the AP walk's roots include AP-only modules absent from the fl DLL). Prints one JSON line;
exit 1 on a violation or when the handler is missing.

usage: far_jet_no_x87.py <fl d3d9.dll>
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


def main():
    functions = x87.disassemble(Path(sys.argv[1]))
    if '_x3m_engine_far_jet' not in functions:
        print(json.dumps(dict(result='FAIL', error='no _x3m_engine_far_jet')))
        return 1
    seen = x87.walk(functions, ['_x3m_engine_far_jet'])
    violations = {name: lines for name, lines in seen.items() if lines}
    simd = {name: [l.strip() for l in functions[name] if re.search(r'%x?mm\d', l)] for name in seen}
    simd = {name: lines for name, lines in simd.items() if lines}
    body = [x87.parse(line) for line in functions['_x3m_engine_far_jet']]
    offsets = sorted({m.group(1) for _, ops in body if ops for m in [re.search(r'(0x14[04])\(', ops)] if m})
    bad = violations or simd or offsets != ['0x144']
    print(json.dumps(dict(result='FAIL' if bad else 'PASS', walked=len(seen), violations=violations, simd=simd,
                          model_offsets_read=offsets)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
