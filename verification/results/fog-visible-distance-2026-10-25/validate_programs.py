#!/usr/bin/env python3
"""Slot counts and the native Windows D3D9 validator for the fog_distance programs (and the look programs they extend).

Writes each program's bytecode from its checked-in header to <scratch>, builds
verification/results/hull-widen-native-validator/native_validate.cpp with MinGW and runs it through WSL interop
(SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9: no device, no window, no game). Also counts the distinct constant
registers each instruction reads (the ps_3_0 one-read-port rule the validator enforces). Prints one row per program
and writes validate_programs.json beside this script (names, slots, verdicts, messages only).
usage: python3 validate_programs.py <scratch dir>
"""
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import fog_density_shader_slots as slots  # noqa: E402

NAMES = ('fog_density_march_look', 'fog_density_repair_look', 'fog_density_march_look_q4', 'fog_density_repair_look_q4',
         'fog_density_march_distance', 'fog_density_repair_distance', 'fog_density_march_distance_q4',
         'fog_density_repair_distance_q4')


def constant_reads(words):
    """Most distinct c# registers read by one instruction (source parameter register type 2 = const)."""
    worst, i = 0, 1
    while i < len(words) - 1:
        token = words[i]
        if token & 0xffff == 0xfffe:
            i += 1 + ((token >> 16) & 0x7fff)
            continue
        op, n = token & 0xffff, (token >> 24) & 15
        if op not in (31, 81, 47, 48):  # dcl, def, defb, defi
            regs = set()
            for p in words[i + 2:i + 1 + n]:  # skip the destination
                kind = ((p >> 28) & 7) | ((p >> 8) & 0x18)
                if kind == 2:
                    regs.add(p & 0x7ff)
            worst = max(worst, len(regs))
        i += 1 + n
    return worst


def main():
    scratch = Path(sys.argv[1]).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    exe = scratch / 'native_validate.exe'
    subprocess.run(['i686-w64-mingw32-g++', '-O1', '-static', str(ROOT / 'verification/results/hull-widen-native-validator/native_validate.cpp'),
                    '-o', str(exe)], check=True)
    rows, files = {}, []
    for name in NAMES:
        words = slots.words_of(ROOT / ('src/renderer/%s_program_inc.h' % name))
        path = scratch / (name + '.bin')
        path.write_bytes(struct.pack('<%dI' % len(words), *words))
        files.append(path)
        rows[name] = dict(slots.count(words), max_constant_reads=constant_reads(words))
    out = subprocess.run([str(exe)] + [p.name for p in files], cwd=scratch, check=True, capture_output=True, text=True).stdout
    current = None
    for line in out.splitlines():
        m = re.match(r'(\S+)\.bin words=(\d+)', line)
        if m:
            current = rows[m.group(1)]
            current['messages'] = []
            continue
        m = re.search(r'msg instr=(-?\d+) id=(\d+): (.*)', line)
        if m and current is not None:
            current['messages'].append(m.group(3).strip())
        m = re.search(r'end=(\w+) messages=(\d+)', line)
        if m and current is not None:
            current['end'] = m.group(1)
    for name, r in rows.items():
        print('%-34s slots=%4d tex=%2d loops=%d words=%4d max_c_reads=%d end=%s messages=%d' % (
            name, r['slots'], r['texture_instructions'], r['loops'], r['words'], r['max_constant_reads'], r.get('end'), len(r.get('messages', []))))
    record = dict(validator='SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9 (native Windows 11 host via WSL interop, no device)',
                  slot_table='verification/probe/fog_density_shader_slots.py (Microsoft ps_3_0 table)', programs=rows)
    (HERE / 'validate_programs.json').write_text(json.dumps(record, indent=1) + '\n')
    return 0 if all(r.get('end') == '00000000' and not r.get('messages') and r['max_constant_reads'] <= 1 for r in rows.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
