#!/usr/bin/env python3
"""Native Windows run 2026-10-11, x3m.log session 20261011-202736 (Run143 DLL b7839952, perf=1): cockpit fog flicker.
Usage: python3 cockpit_gate_query.py /mnt/c/X3/x3m.log
Per-frame join of volumetric_fog_frame reason with the volumetric_fog_cards outcome; run-length statistics per
view stretch (stretches from the 300-frame chase_camera applied/refused/refused_inactive counters, see STRETCHES);
the refused card state vectors (volumetric_fog_card_states, once per 300 frames on refusal); per-class ranges of
the card counts. Prints counts only."""
import re, sys, collections
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
# Derived from chase_camera rows (frame, applied, refused deltas); boundaries +-0 where the counter arithmetic closes.
STRETCHES = [('chase_1', 1736, 11704), ('cockpit_A', 11705, 17202), ('chase_2', 17203, 25376),
             ('chase_inactive_B', 25377, 26288), ('cockpit_B', 26289, 26531), ('chase_3', 26532, 29132)]
card, fog, sea, states = {}, {}, {}, collections.Counter()
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('volumetric_fog_cards '):
        d = kv(l); f = int(d['frame'])
        cls = d['refusal'] if d['refusal'] != 'none' else ('ok' if d['applied'] == '1' else 'idle') + d['mode']
        card[f] = (cls, int(d['observed']), int(d['suppressed']), int(d['refused']))
    elif l.startswith('volumetric_fog_frame'):
        d = kv(l); fog[int(d['frame'])] = (d['reason'], d['applied'], d['shadow_maps'], d['calls'])
    elif l.startswith('screen_emission_additive_frame') and 'device=1' in l:
        d = kv(l); sea[int(d['frame'])] = d['pairs']
    elif l.startswith('volumetric_fog_card_states'):
        d = kv(l); states[' '.join(f'{k}={d[k]}' for k in 'z zwrite atest blend mask cull stencil fill src dst op sepalpha'.split())] += 1
print('joint (card class, fog reason):', collections.Counter((card.get(f, ('?',))[0], r[0]) for f, r in fog.items()).most_common())
print('refused card state vectors:', dict(states))
def runs(lo, hi):
    out, prev = [], None
    for f in range(lo, hi + 1):
        if f not in card: continue
        k = card[f][0]
        if k != prev: out.append([k, 0]); prev = k
        out[-1][1] += 1
    return out
for name, lo, hi in STRETCHES:
    r = runs(lo, hi); n = sum(x[1] for x in r)
    by = collections.defaultdict(list)
    for k, L in r: by[k].append(L)
    print(f'{name} {lo}-{hi}: frames={n} runs={len(r)} transitions={len(r)-1} per_100={100*(len(r)-1)/max(n,1):.2f}')
    for k, Ls in by.items():
        print(f'   {k}: frames={sum(Ls)} runs={len(Ls)} short(<=6)={sum(1 for L in Ls if L<=6)} min={min(Ls)} median={sorted(Ls)[len(Ls)//2]} max={max(Ls)}')
    cls = collections.defaultdict(lambda: collections.defaultdict(list))
    for f in range(lo, hi + 1):
        if f in card:
            c = card[f]; cls[c[0]]['observed'].append(c[1]); cls[c[0]]['suppressed'].append(c[2]); cls[c[0]]['refused'].append(c[3])
            cls[c[0]]['additive_pair'].append(int(sea.get(f, '000') != '000'))
            fr = fog.get(f)
            if fr: cls[c[0]]['fog_applied'].append(int(fr[1])); cls[c[0]]['shadow_maps'].append(int(fr[2])); cls[c[0]]['calls'].append(int(fr[3]))
    for k, v in cls.items():
        print(f'   {k} ranges:', {n: (min(x), max(x)) if n != 'additive_pair' else sum(x) for n, x in v.items()})
