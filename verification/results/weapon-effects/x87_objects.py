#!/usr/bin/env python3
"""x87/MMX opcode count of the weapon-effects step 1 code in a CMake build tree (the brief's (e); the linked-DLL walk
is verification/probe/check_no_x87.py). Whole objects for the new translation units; inside the shared motion_output
object only the functions whose demangled name mentions the weapon stage (motion_output_weapon_effects_inc.h and the
inlined stage builder), since that object carries other features' pre-existing x87. Also counts each object's undefined
symbols (the handler TU weapon_effects_hook.cpp must have none: no CRT, no Win32 import). The state transport
check_no_x87.py allows (cpu_state.h PreserveCpuState: fnsave, the fninit after it, frstor; fxsave/fxrstor; the read-only
fnstcw) is not counted.
usage: python3 verification/results/weapon-effects/x87_objects.py [build]"""
import pathlib
import re
import subprocess
import sys

build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build')
X87 = re.compile(r'^\s*[0-9a-f]+:\s+(f[a-z0-9]+|emms)\b|\b(st\(\d\)|mm[0-7])\b')
TRANSPORT = re.compile(r'^\s*[0-9a-f]+:\s+(fnsave|fninit|frstor|fxsave|fxrstor|fnstcw)\b')
SYMBOL = re.compile(r'^[0-9a-f]+ <(.*)>:')


def scan(obj):
    listing = subprocess.run(['i686-w64-mingw32-objdump', '-d', '-C', '-Mintel', '--no-show-raw-insn', str(obj)],
                             capture_output=True, text=True, check=True).stdout
    per = {}
    current = ''
    for line in listing.splitlines():
        m = SYMBOL.match(line)
        if m:
            current = m.group(1)
            continue
        if X87.search(line) and not TRANSPORT.search(line):
            per[current] = per.get(current, 0) + 1
    return per


for obj in sorted(build.rglob('*weapon_effects*.obj')):
    per = scan(obj)
    undefined = [u for u in subprocess.run(['i686-w64-mingw32-nm', '-u', str(obj)], capture_output=True,
                                           text=True).stdout.split() if u != 'U']
    print(f'{obj.relative_to(build)} x87={sum(per.values())} undefined={len(undefined)}')
for obj in sorted(build.rglob('motion_output.cpp.obj')):
    per = scan(obj)
    weapon = {k: v for k, v in per.items() if 'weapon' in k.lower()}
    print(f'{obj.relative_to(build)} weapon_functions_x87={sum(weapon.values())} {sorted(weapon)[:3]} '
          f'(whole object, other features: {sum(per.values())})')
