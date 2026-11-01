#!/usr/bin/env python3
"""Write/caller census and AP side-by-side witnesses for docs/reverse-engineering/fl/sector-fog.md section 6.

Linear capstone sweep of .text of X3FL.exe and X3AP.exe (AP base 0x400000, not relocatable). Prints
addresses and counts only (preferred VAs). A linear sweep can miss code reached only through data; the
census is direct-operand evidence, not an alias proof.

usage: census.py [FL_EXE] [AP_EXE]
"""
import importlib.util
import re
import sys
from pathlib import Path

import capstone

ROOT = Path(__file__).resolve().parents[4]
_spec = importlib.util.spec_from_file_location('idc', ROOT / 'tools/analysis/fl_identity_constants.py')
idc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(idc)


def sweep(path):
    data = Path(path).read_bytes()
    fields = idc.parse(data)
    _, vsize, va, raw_size, raw_ptr, _ = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.text')
    code = data[raw_ptr:raw_ptr + min(vsize, raw_size)]
    base = fields['image_base'] + va
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out, off = [], 0
    while off < len(code):
        for address, size, mnemonic, ops in md.disasm_lite(code[off:], base + off):
            out.append((address, mnemonic, ops))
            off = address + size - base
        if off < len(code):
            off += 1
    return out


def callers(ins, target):
    return [hex(a) for a, m, o in ins if m in ('call', 'jmp') and o == hex(target)]


def writes(ins, pattern):
    rx = re.compile(pattern)
    return [f'{a:#x} {m} {o}' for a, m, o in ins
            if m in ('mov', 'add', 'sub', 'inc', 'dec', 'and', 'or', 'xor') and rx.match(o)]


fl = sweep(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL/X3FL.exe')
ap = sweep(sys.argv[2] if len(sys.argv) > 2 else '/mnt/c/X3AP-orig/X3AP.exe')
print('FL instructions', len(fl), 'AP instructions', len(ap))
for name, target in (('type loader', 0x43e710), ('type cleanup', 0x442b80), ('simulation restore', 0x448bb0),
                     ('background change', 0x45c2a0), ('sector space setter', 0x421c70), ('cockpit updater', 0x421f50),
                     ('cockpit update loop', 0x41e550), ('complete render pass', 0x48b6d0), ('name helper', 0x50b670)):
    found = callers(fl, target)
    print(f'FL callers {name} {target:#x}: {len(found)}', ' '.join(found[:6]))
print('FL writers table/count slots:', writes(fl, r'dword ptr \[(0x5a83d0|0x5a8338|e..\*4 \+ 0x5a83c8|e..\*4 \+ 0x5a8330)\](, |$)'))
print('FL writers cockpit registry slot:', writes(fl, r'dword ptr \[0x5a82e4\], '))
print('AP writers table/count slots:', writes(ap, r'dword ptr \[(0x606fc0|0x607040|e..\*4 \+ 0x606fb8|e..\*4 \+ 0x607038)\](, |$)'))
print('AP writers cockpit registry slot:', writes(ap, r'dword ptr \[0x608504\], '))
print('FL writers config+0x758:', writes(fl, r'dword ptr \[e.. \+ 0x758\], '))
print('AP writers config+0x768:', writes(ap, r'dword ptr \[e.. \+ 0x768\], '))
# The AP fog-flag branch: the last flag-setting instruction before the jl at 0x42157a.
idx = next(k for k, (a, _, _) in enumerate(ap) if a == 0x42157a)
setter = next((a, m, o) for a, m, o in reversed(ap[idx - 8:idx]) if m not in ('mov', 'lea'))
print(f'AP jl 0x42157a ({ap[idx][2]}) flags from {setter[0]:#x} {setter[1]} {setter[2]}; '
      f'other branches to 0x4215a0: {[hex(a) for a, m, o in ap if m.startswith("j") and o == "0x4215a0" and a != 0x42157a]}')
