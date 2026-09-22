#!/usr/bin/env python3
"""ps_3_0 instruction-slot count of the stored-density fog programs.

Microsoft's ps_3_0 table including flow control (loop/rep/if/ifc/breakc/callnz 3,
endloop/endrep/call/texkill 2, texldl 2, texldd 3, texldb 6), which
ambient_occlusion_program_slots charges one slot each. Static cost only.
https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-3-0
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = {
    'fog_density_march': ROOT / 'src/renderer/fog_density_march_program_inc.h',
    'fog_density_composite': ROOT / 'src/renderer/fog_density_composite_program_inc.h',
    'fog_density_repair': ROOT / 'src/renderer/fog_density_repair_program_inc.h',
    'fog_density_march_exact': ROOT / 'verification/probe/fog_density_march_exact_program_inc.h',
}
# The single look (FOG_LOOK), the three programs the renderer draws: the same 512-slot ceiling as the
# unshaped parity programs above (CrossOver reports exactly 512).
LOOK_PROGRAMS = {name: ROOT / ('src/renderer/%s_program_inc.h' % name) for name in (
    'fog_density_march_look', 'fog_density_composite_look', 'fog_density_repair_look')}
PROGRAMS.update(LOOK_PROGRAMS)
# The sun-visibility slice grid (X3M_FOG_SHADOW_PASS=1): the pass and the look's march/repair reading it.
GRID_PROGRAMS = {name: ROOT / ('src/renderer/%s_program_inc.h' % name) for name in (
    'fog_density_visibility_grid', 'fog_density_march_grid', 'fog_density_repair_grid')}
PROGRAMS.update(GRID_PROGRAMS)
ZERO = {31, 48, 81, 47, 30}                    # dcl, defi, def, defb, label
COST = {37: 8, 66: 6,                          # sincos, texldb
        # loop rep if ifc breakc callnz nrm pow texldd breakp
        27: 3, 38: 3, 40: 3, 41: 3, 45: 3, 26: 3, 36: 3, 32: 3, 93: 3, 96: 3,
        # endloop endrep call texkill crs dp2add lrp dsx dsy texldl
        29: 2, 39: 2, 25: 2, 65: 2, 33: 2, 90: 2, 18: 2, 91: 2, 92: 2, 95: 2,
        20: 4, 21: 3, 22: 4, 23: 3, 24: 2}     # m4x4 m4x3 m3x4 m3x3 m3x2
TEXTURE = {66, 93, 95, 0x42}                   # texldb, texldd, texldl, texld


def words_of(path):
    return [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', Path(path).read_text())]


def count(words):
    if len(words) < 2 or words[0] != 0xffff0300 or words[-1] != 0xffff:
        raise ValueError('expected a complete ps_3_0 program')
    slots = fetches = loops = 0
    i = 1
    while i < len(words) - 1:
        token = words[i]
        if token & 0xffff == 0xfffe:
            i += 1 + ((token >> 16) & 0x7fff)
            continue
        op, operands = token & 0xffff, (token >> 24) & 15
        slots += 0 if op in ZERO else COST.get(op, 1)
        fetches += op in TEXTURE
        loops += op in (27, 38)  # loop, rep: the 64-bin march must stay a loop
        i += 1 + operands
    if i != len(words) - 1:
        raise ValueError('instruction framing')
    return dict(slots=slots, texture_instructions=fetches, words=len(words), loops=loops)


def main():
    rows = {name: count(words_of(path)) for name, path in PROGRAMS.items()}
    print(json.dumps(rows, indent=1))
    return 0 if all(r['slots'] < 512 for r in rows.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
