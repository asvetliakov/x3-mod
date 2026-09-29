#!/usr/bin/env python3
"""Run 358 motion_route rows on capture frames: count per (gate, unmatched) and per pixel shader for the rest bursts;
session totals of motion_output_frame counters; motion_lifetime row count (capture-frame per-draw lifetime rows are
logged only while object_lifetime observation is active). Usage: routes.py <dir> first last [first last ...]"""
import glob, re, sys, collections
d = sys.argv[1]; rng = [(int(sys.argv[i]), int(sys.argv[i + 1])) for i in range(2, len(sys.argv), 2)]
L = glob.glob(f'{d}/session-*.log')[0]
gate = collections.Counter(); ps = collections.defaultdict(collections.Counter); tot = collections.Counter(); life = 0; nf = 0; matchedf = 0
for l in open(L, errors='replace'):
    if l.startswith('motion_route device=1 '):
        f = int(re.search(r' frame=(\d+)', l).group(1))
        if any(a <= f <= b for a, b in rng):
            g = re.search(r' gate=(\d+)', l).group(1); u = re.search(r' unmatched=(\S+)', l); p = re.search(r' ps=(\w+)', l).group(1)
            gate[(g, u.group(1) if u else '-')] += 1; ps[p][g] += 1
    elif l.startswith('motion_output_frame device=1 '):
        nf += 1; g = {k: int(v) for k, v in re.findall(r' (draws|routed|matched|gate[1-6])=(\d+)', l)}; tot.update(g); matchedf += g['matched'] > 0
    elif l.startswith('motion_lifetime '): life += 1
print('capture-frame motion_route (gate, unmatched):', dict(gate))
for p, c in sorted(ps.items(), key=lambda x: -sum(x[1].values())): print(f'  ps {p}: {dict(c)}')
print(f'session motion_output_frame rows {nf}: {dict(tot)}; frames with matched>0: {matchedf}; motion_lifetime rows: {life}')
