#!/usr/bin/env python3
"""Cost model of --shadow-alpha-casters from a session log (read-only).
Per frame from shadow_replay_candidates: excluded= (alpha-tested z-writing routed draws the
origin rule admitted: the candidates the option adds, before the bounds test the option also
runs for them) against leased= (today's leased casters). Capture frames: object_bounds rows with
alpha_tested=1 (every alpha-tested z-writing managed draw with a known extent, admitted or not).
Usage: python3 alpha_casters.py LOG"""
import re, sys, statistics
log = sys.argv[1]
excl, leased, routed = [], [], []
ob = {}
pat = re.compile(rb' (excluded|leased|routed)=(\d+)')
with open(log, 'rb') as f:
    for line in f:
        if line.startswith(b'shadow_replay_candidates '):
            d = {k.decode(): int(v) for k, v in pat.findall(line)}
            excl.append(d['excluded']); leased.append(d['leased']); routed.append(d['routed'])
        elif line.startswith(b'object_bounds ') and b' alpha_tested=1' in line:
            fr = int(re.search(rb' frame=(\d+)', line).group(1))
            ob[fr] = ob.get(fr, 0) + 1
def q(v, p): s = sorted(v); return s[min(len(s) - 1, int(p * len(s)))]
print(f'frames={len(excl)}')
for name, v in (('excluded', excl), ('leased', leased), ('routed', routed)):
    print(f'{name}: mean={statistics.mean(v):.1f} p50={q(v,.5)} p95={q(v,.95)} max={max(v)} frames_nonzero={sum(1 for x in v if x)}')
ratio = [e / l for e, l in zip(excl, leased) if l]
print(f'excluded/leased: mean={statistics.mean(ratio):.3f} p95={q(ratio,.95):.3f}')
if ob:
    print(f'capture frames with alpha_tested object_bounds rows: {len(ob)}; rows per frame min={min(ob.values())} max={max(ob.values())} mean={statistics.mean(ob.values()):.1f}')
