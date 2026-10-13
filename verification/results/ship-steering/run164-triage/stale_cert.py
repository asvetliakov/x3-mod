# B.4 item 3: why a member pair's clear certificate read why=stale at E. For every e_pass why=stale row: the frame's game step and the
# previous two (from any ship_ row's frame/t_ms), the bound E applied if the step was not noted before E in that frame
# (250 + max(prev, prevprev), gc::age_bound) vs the one with it noted (250 + max(step, prev)), and the lower bound of the verdict's age
# (the step itself: the last sweep ran in an earlier frame). Also the A_age_ms of ship-vs-ship E rows (E before A's site B: A_age = the step).
# Usage: python3 -I stale_cert.py x3m.log  (streams; prints only the rows below)
import sys, re
t = {}; stale = []; ages = []
for l in open(sys.argv[1], 'rb'):
    if not l.startswith(b'ship_'): continue
    m = re.search(rb' frame=(\d+) t_ms=(\d+)', l)
    if not m:
        m2 = re.search(rb' frame=(\d+) ', l)
        if m2 and l.startswith(b'ship_avoid_e_verdict ') and b'B_class=7 ' in l:
            f = int(m2.group(1)); a = re.search(rb' A_age_ms=(-?\d+)', l)
            if a: ages.append((f, int(a.group(1))))
        continue
    f, tm = int(m.group(1)), int(m.group(2))
    t.setdefault(f, tm)
    if l.startswith(b'ship_gate_ctl_pair ') and b'kind=e_pass' in l and b'why=stale' in l:
        d = dict(re.findall(rb'(\w+)=(\S+)', l)); stale.append((f, d[b'a'].decode(), d[b'b'].decode(), d[b'd_m'].decode()))
def step(f): return t[f] - t[f - 1] if f in t and f - 1 in t else None
print('why=stale e_pass rows', len(stale))
seen = set(); n_unnoted = 0; n_other = 0
for f, a, b, d in stale:
    k = (f, min(a, b), max(a, b))
    if k in seen: continue
    seen.add(k)
    s0, s1, s2 = step(f), step(f - 1), step(f - 2)
    if None in (s0, s1, s2): print('f %d %s %s d_m %s steps unknown' % (f, a, b, d)); continue
    unnoted = s0 > 250 + max(s1, s2) and s0 <= 250 + max(s0, s1)
    n_unnoted += unnoted; n_other += not unnoted
    print('f %6d %s %s d_m %5s step %4d prev %4d,%4d | bound unnoted %4d noted %4d | min age %4d -> %s' % (f, a, b, d, s0, s1, s2,
          250 + max(s1, s2), 250 + max(s0, s1), s0, 'stale by the unnoted step' if unnoted else 'other'))
print('pairs-frames: stale by the unnoted step %d, other %d' % (n_unnoted, n_other))
eq = sum(1 for f, x in ages if step(f) is not None and x == step(f))
print('ship-vs-ship E rows with A_age_ms == the frame step (E before A site B):', eq, 'of', sum(1 for f, x in ages if step(f) is not None))
