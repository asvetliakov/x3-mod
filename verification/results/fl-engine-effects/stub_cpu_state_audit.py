#!/usr/bin/env python3
"""FL engine-effects stubs CPU-state audit (src/engine/fl/engine_effects_fl.cpp; docs/architecture/
engine-effects-modern.md, "FL port, step 1").

Disassembles a built image (the fl d3d9.dll or the native fixture verification/probe/fl_engine_effects.cpp) with
verification/probe/check_no_x87.py's objdump reader and requires each stub to be exactly the integer sequence the
site contract allows, up to its padding: `push eax; mov eax,[esp+N]; cmpw $7,0x48(eax); pop eax; je skip;
jmp *continue; xor eax,eax; ret[ $0x10]` with N = 0xc (A: obj [esp+8] at entry) or 0x10 (B: obj [esp+0xc]). Hence no
call (no Win32 API: LastError untouched), no x87/MMX/SSE register or instruction (XMM, x87, MXCSR untouched), no write
other than the EAX save on the stack, and no ECX/EDX/EBX/ESI/EDI/EBP operand.

usage: stub_cpu_state_audit.py <image>   (exit 1 on a finding; prints one JSON line)
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
STUBS = {'_x3m_engine_effects_stub_a': ('0xc', 'ret'), '_x3m_engine_effects_stub_b': ('0x10', 'ret $0x10')}
FORBIDDEN = re.compile(r'%(?:x|y)mm\d|%mm\d|%st|%e?(?:cx|dx|bx|si|di|bp)\b|%[cdb][lh]\b')


def expected(offset, ret):
    return [('push', '%eax'), ('mov', f'{offset}(%esp),%eax'), ('cmpw', '$0x7,0x48(%eax)'), ('pop', '%eax'),
            ('je', None), ('jmp', '*'), ('xor', '%eax,%eax'), tuple(ret.split(' ', 1)) if ' ' in ret else ('ret', '')]


def main():
    functions = x87.disassemble(Path(sys.argv[1]))
    out = {'stubs': {}, 'findings': []}
    for name, (offset, ret) in STUBS.items():
        body = [x87.parse(line) for line in functions.get(name, [])]
        body = [(m, ops.split('#')[0].strip()) for m, ops in body if m]
        while body and body[-1][0] in ('nop', 'xchg', 'lea', 'int3'):   # alignment padding after the last ret
            body.pop()
        want = expected(offset, ret)
        ok = len(body) == len(want)
        for (m, ops), (wm, wops) in zip(body, want):
            if m != wm or (wops is not None and wops != '*' and ops.replace('DWORD PTR ', '') != wops):
                ok = False
            if wops == '*' and not ops.startswith('*'):
                ok = False
            if FORBIDDEN.search(ops) or x87.parse('0: ' + m + ' ' + ops)[0] in ('call', 'lcall'):
                ok = False
        out['stubs'][name] = {'instructions': len(body), 'matches_contract': ok,
                              'mnemonics': [m for m, _ in body]}
        if not ok:
            out['findings'].append(name)
    print(json.dumps(out))
    return 1 if out['findings'] or len(out['stubs']) != 2 else 0


if __name__ == '__main__':
    sys.exit(main())
