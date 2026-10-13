# Run163 Q2/Q5: frames whose game-time step exceeds a threshold (default 1000 ms = kStateMaxAgeMs), from consecutive ship_trace t_ms,
# with the SETA flag and the real dt; counts overall and in a frame window. Usage: python3 -I game_steps.py rows.txt [--min 1000] [--from F --to F]
import sys, re
a = sys.argv; mn = int(a[a.index('--min') + 1]) if '--min' in a else 1000
f0 = int(a[a.index('--from') + 1]) if '--from' in a else 0; f1 = int(a[a.index('--to') + 1]) if '--to' in a else 10**9
g = {}; seta = {}; dt = {}
for l in open(a[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); g.setdefault(int(m[1]), int(m[2]))
    elif l.startswith('engine_stage ') and 'seta=' in l:
        m = re.search(r' frame=(\d+).*seta=(\d)', l); seta[int(m[1])] = m[2]
    elif l.startswith('frame_end '):
        m = re.search(r' frame=(\d+) .*dt_ms=(\d+)', l)
        if m: dt[int(m[1])] = int(m[2])
fs = sorted(g); big = []; hist = {}
for p, f in zip(fs, fs[1:]):
    if f != p + 1: continue
    s = g[f] - g[p]; b = min(s // 250 * 250, 3000); hist[b] = hist.get(b, 0) + 1
    if s > mn and f0 <= f <= f1: big.append((f, s, seta.get(f), dt.get(p), dt.get(f)))
print('game step histogram (ms bucket: frames, consecutive traced frames):', dict(sorted(hist.items())))
print('steps > %d ms in window: %d' % (mn, len(big)))
for f, s, st, d0, d1 in big: print('f %d step_ms %d seta %s real dt prev %s this %s' % (f, s, st, d0, d1))
