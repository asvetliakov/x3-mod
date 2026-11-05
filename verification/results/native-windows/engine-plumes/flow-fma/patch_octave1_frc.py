"""Diagnosis only (2026-11-05): copies the production plume pixel program with ONE instruction changed, the octave-1
`mul r1.xyz, r1, c22.y` (the frc input 2.2 p that fxc folded from frac(2.2 p + (5, 2, 1))) to `add r1.xyz, r3, c21.z`
(frc of the octave-1 coordinate itself, so i = P1 - frc(P1) is an exact integer). Writes the patched _inc.h to argv[1].
Result on the native host (RTX 5070 Ti, 32.0.16.2114): all 21 flow checks PASS, value 1500 ratio 1.0001 corr 0.9991."""
import re, sys
src = open('/home/asvetl/x3-mod/src/renderer/engine_plume_pixel_program_inc.h').read()
words = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{8})u', src)]
old = [0x03000005, 0x80070001, 0x80E40001, 0xA0550016]
new = [0x03000002, 0x80070001, 0x80E40003, 0xA0AA0015]
hits = [k for k in range(len(words) - 3) if words[k:k + 4] == old]
assert len(hits) == 1, hits
words[hits[0]:hits[0] + 4] = new
with open(sys.argv[1], 'w') as out:
    for j in range(0, len(words), 6):
        out.write('    ' + ', '.join('0x%08xu' % w for w in words[j:j + 6]) + ',\n')
