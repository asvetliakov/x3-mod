#!/usr/bin/env python3
"""Write the occlusion cull pass's embedded programs (src/renderer/occlusion_cull_pass.cpp rect_vs_words,
rect_mad_vs_words, rect_ps_head + n MOVs to oC0..oC(n-1), n = 1..4) parsed from the source, for native_validate.exe.
usage: occlusion_programs.py <repo> <out dir>"""
import re, struct, sys
src = open(sys.argv[1] + '/src/renderer/occlusion_cull_pass.cpp').read()
def arr(name):
    body = re.search(name + r'\[\]\s*=\s*\{([^}]*)\}', src).group(1)
    return [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)u', body)]
out = sys.argv[2]
for name in ('rect_vs_words', 'rect_mad_vs_words'):
    w = arr(name); open(f'{out}/occl-{name}.bin', 'wb').write(struct.pack(f'<{len(w)}I', *w)); print(name, len(w))
head = arr('rect_ps_head')
for n in range(1, 5):
    w = head + sum(([0x02000001, 0x800f0800 | t, 0xa0e40000] for t in range(n)), []) + [0x0000ffff]
    open(f'{out}/occl-ps{n}.bin', 'wb').write(struct.pack(f'<{len(w)}I', *w)); print('ps', n, len(w))
