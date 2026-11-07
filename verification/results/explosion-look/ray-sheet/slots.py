"""slots.py <worktree>: compile the scratch ray-sheet program and count the instruction tokens (SM3 token stream: every
instruction except dcl/def/defi/defb, comments, end; macro ops by their documented slot counts) of it, of the sprite
pixel program as built and as on HEAD (git show)."""
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / 'tools/shaders'))
import generate_rigid_motion_pixel as base  # noqa: E402
import generate_weapon_effects_programs as gen  # noqa: E402

MACRO = {0x0E: 1, 0x0F: 1, 0x25: 8, 0x20: 3, 0x24: 3, 0x5B: 1, 0x5C: 1}  # nrm 3 (0x24), pow 3 (0x20), sincos 8 (0x25)
SKIP = {0x1F, 0x51, 0x52, 0x53, 0xFFFE, 0xFFFF}


def count(words):
    i, n, flow = 1, 0, 0
    while i < len(words):
        w = words[i]
        op = w & 0xFFFF
        if op == 0xFFFE:
            i += 1 + ((w >> 16) & 0x7FFF)
            continue
        if op == 0xFFFF:
            break
        ln = (w >> 24) & 0xF
        if op not in SKIP:
            n += MACRO.get(op, 1)
        i += 1 + ln
    return n


def words_of_header(text):
    import re
    return [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', text)]


src = Path(__file__).with_name('ray_sheet_ps.hlsl')
inc = (root / 'src/effects/weapon_soft_depth.hlsl').read_text()
text = src.read_text().replace('#include "weapon_soft_depth.hlsl"', inc)
with tempfile.TemporaryDirectory(prefix='ray-sheet-') as d:
    d = Path(d)
    exe, binary, s = d / 'compile.exe', d / 'program.bin', d / 'ray_sheet_ps.hlsl'
    s.write_text(text)
    subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-msse2', '-mfpmath=sse', '-static', str(base.COMPILER_SOURCE),
                    '-o', str(exe)], check=True)
    gen.run_compiler(exe, (base.bottle.game_dir() / 'd3dx9_37.dll').resolve(), s, binary, 'ps_3_0')
    data = binary.read_bytes()
words = [int.from_bytes(data[i:i + 4], 'little') for i in range(0, len(data), 4)]
print('ray_sheet_ps words', len(words), 'instruction slots', count(words))
now = words_of_header((root / 'src/renderer/weapon_sprite_pixel_program_inc.h').read_text())
head = words_of_header(subprocess.run(['git', '-C', str(root), 'show', 'HEAD:src/renderer/weapon_sprite_pixel_program_inc.h'],
                                      capture_output=True, text=True, check=True).stdout)
print('weapon_sprite_ps built words', len(now), 'slots', count(now), '; HEAD words', len(head), 'slots', count(head))
