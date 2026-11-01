#!/usr/bin/env python3
"""ABI witness of X3FL.exe's fixed-point scaler (RVA 0x12d10) and its eleven direct callers, with capstone
(docs/architecture/farnhams-legacy-scale-overflow.md). Prints one JSON object; exit 1 on a finding.

helper:  the linear disassembly from the entry to the second RET: instruction count, every memory operand,
         the registers written (EBX never; ESI/EDI pushed and popped; EBP restored by mov esp,ebp; pop ebp),
         branches (all internal), exits (plain RET only, no CALL), then int3 padding.
callers: per call site, the last writers of ECX and EDX before it (a `lea`/`add` forming a pointer), the two
         pushes (out, then &c pushed first), and after it the first access to EAX (a load through [eax], or a
         write), ECX, EDX and EFLAGS within the straight-line block (write = dead; `open` = not resolved before
         a branch/call; the replacement returns the helper's ECX/EDX, so no caller depends on the answer).
interior: generator byte scan (rel8/rel32 into (entry, end)) with operand candidates resolved by disassembly.
usage: abi_witness.py [X3FL.exe]
"""
import importlib.util
import json
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('fl_scale_overflow_constants',
                                              ROOT / 'tools/analysis/fl_scale_overflow_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True


def disasm(data, fields, rva, n):
    off = gen.identity.rva_to_offset(fields, rva)
    return list(MD.disasm(data[off:off + n], rva))


def regs(i):
    r, w = i.regs_access()
    return {i.reg_name(x) for x in r}, {i.reg_name(x) for x in w}


def helper(data, fields):
    ins = disasm(data, fields, gen.HELPER_RVA, gen.HELPER_LENGTH)
    end = gen.HELPER_RVA + gen.HELPER_LENGTH
    written = set()
    for i in ins:
        written |= regs(i)[1]
    branches = [(i.address, int(i.op_str, 16)) for i in ins if i.mnemonic.startswith('j')]
    return {
        'instructions': len(ins), 'decoded_to': ins[-1].address + ins[-1].size if ins else 0, 'end': end,
        'memory_operands': sorted({i.op_str.split('ptr ')[1] for i in ins if 'ptr [' in i.op_str}),
        'written': sorted(written - {'eflags'}),
        'branches_internal': all(gen.HELPER_RVA < t < end for _, t in branches), 'branches': len(branches),
        'exits': [f'{i.address:x} {i.mnemonic} {i.op_str}'.strip() for i in ins if i.mnemonic in ('ret', 'retn', 'call')],
        'pushes': [i.op_str for i in ins if i.mnemonic == 'push'],
        'pops': [i.op_str for i in ins if i.mnemonic == 'pop'],
        'divides': [f'{i.address:x}' for i in ins if i.mnemonic == 'idiv'],
    }


def window(data, fields, call, back=0xa0):
    for b in range(back, back - 16, -1):
        ins = disasm(data, fields, call - b, b + 5)
        if ins and ins[-1].address == call:
            return ins
    return None


def caller(data, fields, call):
    before = window(data, fields, call)[:-1]
    out = {'site': f'{call:x}'}
    for reg in ('ecx', 'edx'):
        out[reg] = next((f'{i.address:x} {i.mnemonic} {i.op_str}' for i in reversed(before) if reg in regs(i)[1]), None)
    out['pushes'] = [f'{i.address:x} push {i.op_str}' for i in before if i.mnemonic == 'push'][-2:]
    first = {}
    for i in disasm(data, fields, call + 5, 0x60):
        r, w = regs(i)
        for reg in ('eax', 'ecx', 'edx', 'eflags'):
            if reg not in first and (reg in r or reg in w):
                first[reg] = ('read ' if reg in r else 'write ') + f'{i.address:x} {i.mnemonic} {i.op_str}'
        if i.mnemonic.startswith('j') or i.mnemonic in ('ret', 'call'):
            break
    for reg in ('eax', 'ecx', 'edx', 'eflags'):
        first.setdefault(reg, 'open')
    out['after'] = first
    return out


def interior(data, fields):
    relocated = gen.identity.relocated_rvas(data, fields)
    hits, operands = gen.interior_branches(data, fields, gen.HELPER_RVA, gen.HELPER_LENGTH, relocated)
    resolved = []
    for rva in operands:
        # The candidate is the first byte of a relocated dword: find the instruction that contains it.
        ins = next(i for b in range(1, 12) for i in [disasm(data, fields, rva - b, 16)[0]]
                   if i.address < rva < i.address + i.size)
        resolved.append(f'{rva:x} inside {ins.address:x} {ins.mnemonic} {ins.op_str}')
    return {'hits': [f'{h:x}' for h in hits], 'operand_candidates': resolved,
            'relocations_in_helper': [f'{r:x}' for r in relocated
                                      if r < gen.HELPER_RVA + gen.HELPER_LENGTH and gen.HELPER_RVA < r + 4]}


def main():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else gen.DEFAULT_EXE
    data = exe.read_bytes()
    fields = gen.identity.parse(data)
    d = gen.decode(data, fields)
    h = helper(data, fields)
    callers = [caller(data, fields, c) for c in d['callers']]
    out = {'helper': h, 'callers': callers, 'interior': interior(data, fields)}
    problems = []
    # EBX untouched; EBP/ESI/EDI pushed once and popped on each of the two exits; three idiv.
    if (h['decoded_to'] != h['end'] or not h['branches_internal'] or 'ebx' in h['written']
            or h['pushes'] != ['ebp', 'esi', 'edi'] or h['pops'] != ['edi', 'esi', 'ebp'] * 2 or len(h['divides']) != 3):
        problems.append('helper shape')
    if [e.split(' ', 1)[1] for e in h['exits']] != ['ret', 'ret']:
        problems.append('helper exits')
    for c in callers:
        if not (c['ecx'] and c['edx'] and len(c['pushes']) == 2 and c['after']['eflags'].startswith('write')
                and c['after']['ecx'] != 'read' and not c['after']['edx'].startswith('read')
                and (c['after']['eax'].startswith('read') and '[eax]' in c['after']['eax'] or c['after']['eax'].startswith('write'))):
            problems.append(f'caller {c["site"]}')
    if out['interior']['hits'] or out['interior']['relocations_in_helper']:
        problems.append('interior')
    out['problems'] = problems
    print(json.dumps(out))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
