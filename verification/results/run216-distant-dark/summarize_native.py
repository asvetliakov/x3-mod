"""Fold gen_fade_owner_forms + native_validate.exe output into fade_owner_forms_native.json (names, sizes, verdicts).

usage: python3 summarize_native.py <scratch dir holding gen.txt and native.txt> <out json> <captures dir> <edition>
  AP: ... fade_owner_forms_native.json /mnt/c/X3/x3-modern-captures AP
  FL: ... fade_owner_forms_native_fl.json /mnt/c/X3FL/x3-modern-captures FL
The scratch dir is produced by (<captures dir> as passed here):
  g++ -std=c++17 -O2 -I src/renderer gen_fade_owner_forms.cpp src/renderer/linear_material.cpp \
      src/renderer/material_motion.cpp -o S/gen
  i686-w64-mingw32-g++ -O1 -static ../hull-widen-native-validator/native_validate.cpp -o S/native_validate.exe
  (cd S && ./gen <captures dir> out > gen.txt && ./native_validate.exe out/*.bin > native.txt)
"""
import json
import re
import sys

scratch, target, captures, edition = sys.argv[1:5]
gen = [line.split() for line in open(f'{scratch}/gen.txt') if 'slots=' in line]
native = open(f'{scratch}/native.txt').read()
rows = []
pattern = (r'out/([0-9a-f]{16})-(\w+?)(-strength)?\.bin words=(\d+) begin=(\w+)\n'
           r'  end=(\w+) messages=(\d+) instruction_worst=(\w+)')
for m in re.finditer(pattern, native):
    rows.append({'original': m.group(1), 'form': m.group(2) + (m.group(3) or ''), 'words': int(m.group(4)),
                 'end': m.group(6), 'messages': int(m.group(7)), 'worst': m.group(8)})
slots = [int(g[3].split('=')[1]) for g in gen]
summary = {
    'validator': 'C:/Windows/SysWOW64/d3d9.dll Direct3DShaderValidatorCreate9 (native_validate.exe, WSL interop)',
    'generator': 'gen_fade_owner_forms.cpp (fill 0.01, gain 4 dynamic, widen K4 B4, fade-owner depth fragment, depth on)',
    'captures': f'{captures} ps_*.bin ({edition})',
    'programs': len({r['original'] for r in rows}),
    'validated': len(rows),
    'passed': sum(r['end'] == '00000000' and r['messages'] == 0 and r['worst'] == '00000000' for r in rows),
    'max_owner_form_slots': max(slots) if slots else 0,
    'rows': rows,
}
with open(target, 'w') as f:
    json.dump(summary, f, indent=0)
print(summary['programs'], summary['validated'], summary['passed'], summary['max_owner_form_slots'])
