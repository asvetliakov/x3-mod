# Run162 Q1: petal laps of a traced capital at gate 0x8e4fbdb0 (G, F from gates.txt): a lap = the nose turning from against F
# (nose.F < -0.9) back to along F (> 0.9) while on order 4 to the gate. Per lap: start/end game and real s, lateral and z range (m),
# speed range (m/s). Also the approach before the first lap (from the first order-4 sample). Usage: python3 -I sb_laps.py rows.txt obj [gate] (Run163: gate argument)
import sys, os, re, math; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); obj = sys.argv[2]
GT = {'0x8e7ff4f0': ((4259.0, -18037.0, 31134.0), (0.0, 0.0, 1.0)), '0x8e7ff9d0': ((-7498.0, 10774.0, -57569.0), (0.0, 0.0, -1.0))}  # Run163 gates.txt
gid = sys.argv[3] if len(sys.argv) > 3 else '0x8e7ff4f0'; G, F = GT[gid]; dot = lambda x, y: sum(p * q for p, q in zip(x, y))
S = []
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_trace frame=') or (' obj=' + obj + ' ') not in l: continue
    d = dict(re.findall(r'(\w+)=(\S+)', l))
    if d['order'] != '0x4' or d['tgt'] != gid: continue
    p = [float(x) / 500 for x in d['pos'].split(',')]; fw = [float(x) for x in d['fwd'].split(',')]
    dd = [p[i] - G[i] for i in range(3)]; z = dot(dd, F); lat = math.sqrt(max(0, dot(dd, dd) - z * z))
    S.append((int(d['frame']), int(d['t_ms']) / 1000, z, lat, dot(fw, F), float(d['v']) / 500))
if not S: print(obj, 'no order-4 samples to', gid); sys.exit()
state = 'fwd' if S[0][4] > 0 else 'back'; laps = []; cur = None; first_back = None
for s in S:
    if state == 'fwd' and s[4] < -0.9:
        state = 'back'; first_back = first_back or s
    elif state == 'back' and s[4] > 0.9:
        state = 'fwd'; laps.append(s)
print(obj, 'order-4 samples', len(S), 'first', 'f %d game %.1f real %.1f z %.0f lat %.0f' % (S[0][0], S[0][1], tm.real(S[0][0]) / 1000, S[0][2], S[0][3]))
if first_back: print(' first turn-back (start of holding) f %d game %.1f real %.1f z %.0f lat %.0f' % (first_back[0], first_back[1], tm.real(first_back[0]) / 1000, first_back[2], first_back[3]))
prev = first_back
for i, s in enumerate(laps):
    seg = [x for x in S if prev and prev[0] <= x[0] <= s[0]]
    if seg:
        print(' lap %d end f %d game %.1f real %.1f | %.0f game s %.1f real s | z %.0f..%.0f lat %.0f..%.0f v %.0f..%.0f' % (i + 1, s[0], s[1],
              tm.real(s[0]) / 1000, s[1] - prev[1], (tm.real(s[0]) - tm.real(prev[0])) / 1000, min(x[2] for x in seg), max(x[2] for x in seg),
              min(x[3] for x in seg), max(x[3] for x in seg), min(x[5] for x in seg), max(x[5] for x in seg)))
    prev = s
print(' last order-4 sample f %d game %.1f real %.1f z %.0f lat %.0f noseF %.2f' % (S[-1][0], S[-1][1], tm.real(S[-1][0]) / 1000, S[-1][2], S[-1][3], S[-1][4]))
