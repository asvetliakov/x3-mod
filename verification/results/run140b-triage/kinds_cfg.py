#!/usr/bin/env python3
"""Row kinds per frame (excluding capture frames' bulk kinds) and config/mode row diff. usage: kinds_cfg.py RUN_A RUN_B"""
import sys, glob, re, collections as C
def load(d):
    k = C.Counter(); cfg = {}; n = 0; sect = C.Counter()
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        kind = raw.split(b' ', 1)[0].decode('latin1'); k[kind] += 1
        if kind == 'frame_end': n += 1
        if re.search(r'(_mode|_config|_configured|_source|config_file|cull_small_parts)$', kind) and kind not in cfg:
            cfg[kind] = re.sub(r'(qpc|stub|frame|elapsed_ms|device)=\S+ ?', '', raw.decode('latin1').strip())[:400]
        if kind == 'sector_background': sect[re.sub(rb'.*?(sector=\S+).*', rb'\1', raw.strip()).decode('latin1')[:60]] += 1
    return k, cfg, n, sect
(a, ca, na, sa), (b, cb, nb, sb) = load(sys.argv[1]), load(sys.argv[2])
print('frames', na, nb)
print('kinds only in B:', {x: b[x] for x in b if x not in a}); print('kinds only in A:', {x: a[x] for x in a if x not in b})
print('rate changes >25% (rows/frame A -> B, rate >= 0.01):')
for x in sorted(set(a) & set(b)):
    ra, rb = a[x] / na, b[x] / nb
    if max(ra, rb) >= 0.01 and abs(rb - ra) > 0.25 * max(ra, rb): print(f'  {x}: {ra:.3f} -> {rb:.3f}')
print('config rows differing:')
for x in sorted(set(ca) | set(cb)):
    if ca.get(x) != cb.get(x): print(f'  A {x}: {ca.get(x)}\n  B {x}: {cb.get(x)}')
print('sector A', sa.most_common(3)); print('sector B', sb.most_common(3))
