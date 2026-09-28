#!/usr/bin/env python3
"""Q3: technique -> program identities of the winning shader/<profile>/standard_lighting.fb and the
named constants/samplers each program reads (CTAB names only). With --disassemble DIR the programs are
also disassembled into DIR (keep DIR outside the repository: derived shader text stays local).
  PYTHONPATH=tools/analysis:verification/results/lod-mayhem-refusals \
    python3 standard_lighting_programs.py [--profile 3_0] [--disassemble DIR] > standard_lighting_programs_out.txt"""
import argparse
import hashlib
import struct
from pathlib import Path

import effect_passes as ep
import index_shaders as ix
import sector_fog_census as sfc
import shader_constants as sc

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
ap = argparse.ArgumentParser()
ap.add_argument('--profile', default='3_0')
ap.add_argument('--effect', default='standard_lighting')
ap.add_argument('--disassemble')
a = ap.parse_args()
assets = sfc.Assets(GAME)
data, meta = assets.get(f'shader/{a.profile}/{a.effect}.fb')
print(f'{meta["source"]}:{meta["member"]} sha256 {hashlib.sha256(data).hexdigest()}')
c = ep.parse_container(data)
print('parameters: ' + ' '.join(p.get('name') or '?' for p in c['parameters']))
for (ti, pi, si), (usage, start, size) in sorted(c['state_resources'].items()):
    words = struct.unpack('<%dI' % (size // 4), data[start:start + size // 4 * 4])
    end = ep.shader_end(words, 0) if words else None
    if end is None:
        continue
    code = data[start:start + 4 * end]
    tech = c['techniques'][ti][0]
    kind = 'ps' if words[0] >> 16 == 0xffff else 'vs'
    names = sorted({x['name'] for x in sc.parse_ctab(code)})
    print(f'{tech:12} P{pi} {kind} {ix.fnv1a64(code)} dwords={end} reads: {" ".join(names)}')
    if a.disassemble:
        import sm3_tokens
        out = Path(a.disassemble)
        out.mkdir(parents=True, exist_ok=True)
        (out / f'{a.effect}_{a.profile}_{tech}_{kind}.asm').write_text('\n'.join(sm3_tokens.dis(code)) + '\n')
