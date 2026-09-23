#!/usr/bin/env python3
"""Compact bench_out.txt from the raw mask_bench.exe outputs (kept local).
Usage: extract_bench_out.py RAW_DIR > bench_out.txt, where RAW_DIR holds final12.txt (old vs final: identity sweep over
scenes 0-3 and every constant set, then timing; run with --scenes 0123 --time 30 15) and timing5.txt / timing6.txt (the
cost-only profile variants of make_profile.py / make_profile2.py)."""
import re
import sys
from pathlib import Path

raw = Path(sys.argv[1])
lines = {name: (raw / name).read_text().splitlines() for name in ('final12.txt', 'timing5.txt', 'timing6.txt')}
final = lines['final12.txt']
ident = [l for l in final if l.startswith('IDENTITY ')]
final_rows = [l for l in ident if ' pass=2 ' in l]
plain_rows = [l for l in ident if 'plain#' in l]
inter = [l for l in ident if 'camera#' in l and ' pass=2 ' not in l]
print('# mask_bench.cpp outputs, bottle X3 (CrossOver Preview), 2026-09-24; old = HEAD cc34f2f1 shaders, final = this change.')
print('## identity sweep, old vs final: 4 scenes (sky, hull, hostile, hull bright) x 11 constant sets x 2 programs x 3 draws')
for l in final:
    if l.startswith(('ADAPTER', 'PROGRAM', 'RESULT')):
        print(l)
print('final_target_rows_identical=%d of %d' % (sum(l.endswith('differing=0') for l in final_rows), len(final_rows)))
print('plain_program_rows_differing=%d of %d' % (sum(not l.endswith('differing=0') for l in plain_rows), len(plain_rows)))
odd = [l for l in inter if not l.endswith('differing=0') and not re.search(r'count=\d+,0,0,0 maxdiff=1,0,0,0$', l)]
print('camera_intermediate_rows=%d differing_beyond_one_b_code=%d (the intermediate b carries the class code by design;'
      ' RESULT counts those bytes)' % (len(inter), len(odd)))
for l in final:
    if l.startswith('CONTENT ') and 'config=flown ' in l and 'camera#' in l:
        print(l)
print('## timing, flown constants: chain = 3 draws back to back, *_alt = one draw repeated alternating targets; N=30, R=15; ms')
for l in final:
    if l.startswith('TIMING ') and 'same_target' not in l:
        f = dict(re.findall(r'(\w+)=(\S+)', l))
        print(f['scene'], f['program'], f['pass'], 'min', f['min_ms'], 'median', f['median_ms'])
print('## timing, one chain / one draw per event wait (the in-game bracketing), 301 rounds, 5%-trimmed means, floor subtracted; ms')
print('## (single draws are quantised in ~0.115 ms steps by the event polling; the chain row and the *_alt rows are the stable ones)')
for l in final:
    if l.startswith('SINGLE '):
        print(l.split(' (')[0])
print('## profile, cost only (median ms, alternating targets; cur = an intermediate state of the shader, see make_profile.py);')
print('## valid rows: tests_alt for tst/nofrag/nogate/noemis/nomotion, x_alt / y_compose_alt for sep/unr/sepunr/noclassfetch/depthonly')
for name in ('timing5.txt', 'timing6.txt'):
    for l in lines[name]:
        if l.startswith('TIMING ') and 'same_target' not in l and 'program=null' not in l:
            f = dict(re.findall(r'(\w+)=(\S+)', l))
            print(name, f['scene'], f['program'], f['pass'], f['median_ms'])
