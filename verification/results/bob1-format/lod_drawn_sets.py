#!/usr/bin/env python3
"""Which LOD records of each installed body can actually be drawn, per the
0047cfe0 selection (docs/reverse-engineering/lod-selection.md, "What the
selection really does"). Input: the JSON of `tools/analysis/bob1.py audit --json`.

  loop  sel = highest i in 1..n-1 with s < trunc(T_i*f), else 0   (0047d429..0047d46e)
  adj   +1 if view+0x270 & 0x1000000; else -1 if cfg+0x768 >= 3   (0047d472..0047d499)
  clamp to [0, n-1]; cfg+0x768 > 3 forces 0                       (0047d4a0..0047d4d1)

R = loop-reachable indices: 0, and i >= 1 with trunc(T_i f) >= 2 (s >= 1) and
trunc(T_i f) > trunc(T_j f) for every j > i. Drawn sets per case:
  main_le2 = R;  main_vh = {max(0,r-1)};  flag_view = {min(r+1,n-1)}  (cfg+0x768 <= 3)

Usage: python3 lod_drawn_sets.py <audit.json> [f ...]   (default f = 1 2)
"""
import json, sys

def reach(T, f):
    n = len(T) + 1
    t = [None] + [int(x * f) for x in T]          # ftol truncation (0x0052b5d0)
    R = {0}
    for i in range(1, n):
        if t[i] >= 2 and all(t[i] > t[j] for j in range(i + 1, n)):
            R.add(i)
    return n, R

def main():
    bodies = [b for b in json.load(open(sys.argv[1]))['bodies'] if b.get('lods', 0) >= 2]
    fs = [float(x) for x in sys.argv[2:]] or [1.0, 2.0]
    print(f'multi_lod_bodies={len(bodies)}')
    for f in fs:
        c = dict(last_dead_vh=0, ladder_dead_vh=0, dead_all_main=0, dead_records_all_main=0,
                 dead_vh=0, drawn_coarsest_multi_vh=0, drawn_coarsest_multi_le2=0,
                 nonmono_dead_all_main=0)
        rows = []
        for b in bodies:
            n, R = reach(b['thresholds'], f)
            vh = {max(0, r - 1) for r in R}
            fl = {min(r + 1, n - 1) for r in R}
            le2 = R
            g = b['groups']
            c['last_dead_vh'] += (n - 1) not in vh
            c['ladder_dead_vh'] += vh == {0}
            dead_main = [i for i in range(1, n) if i not in le2 and i not in vh]
            c['dead_all_main'] += bool(dead_main)
            c['dead_records_all_main'] += len(dead_main)
            c['dead_vh'] += len([i for i in range(1, n) if i not in vh])
            c['drawn_coarsest_multi_vh'] += g[max(vh)] > 1
            c['drawn_coarsest_multi_le2'] += g[max(le2)] > 1
            if b['non_monotonic']:
                c['nonmono_dead_all_main'] += bool(dead_main)
                rows.append(f"  {b['member']} T={b['thresholds']} g={g} R={sorted(R)} "
                            f"main_le2={sorted(le2)} main_vh={sorted(vh)} flag_view={sorted(fl)} "
                            f"dead_all_main={dead_main}")
        print(f'f={f:g} ' + ' '.join(f'{k}={v}' for k, v in c.items()))
        print('\n'.join(rows))

if __name__ == '__main__':
    main()
