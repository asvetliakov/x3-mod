#!/usr/bin/env python3
"""Validation witness for the ps_2_0 no-gap rule: insert `mov oC1, r0` right after the
flag variant's `mov oC0, r0` (RT1 is bound with COLORWRITEENABLE1 = 0, so the value is
discarded). usage: patch_screen_flag_oc1.py in.bin out.bin"""
import struct, sys
w = list(struct.unpack(f'<{len(open(sys.argv[1], "rb").read()) // 4}I', open(sys.argv[1], 'rb').read()))
MOV_OC0 = [0x02000001, 0x800F0800, 0x80E40000]          # mov oC0, r0
hits = [i for i in range(len(w) - 2) if w[i:i + 3] == MOV_OC0]
assert len(hits) == 1, hits
w[hits[0] + 3:hits[0] + 3] = [0x02000001, 0x800F0801, 0x80E40000]  # mov oC1, r0
open(sys.argv[2], 'wb').write(struct.pack(f'<{len(w)}I', *w))
print(sys.argv[2], 'inserted at dword', hits[0] + 3, 'words', len(w))
