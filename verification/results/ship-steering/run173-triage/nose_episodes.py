# Run173: episodes where a convoy ship's nose is > 30 deg off the gate's F, per order segment, from ship_trace rows.
# F = world (0,0,-1): derived from the rewrite rows (KC grid point -> L_i differs by world (0,-997 m,-2920 m*-1) = gate frame (0,-997,+2920)).
# Usage: grep -a 'ship_trace ' x3m.log | python3 -I nose_episodes.py OBJ [from_frame]
import sys, re, math
obj = sys.argv[1]; lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
seg = None
def flush(s):
    if s: print('%s f=%d-%d game=%.1f-%.1f (%.1f s) order=%s nose_max=%.0f nose_end=%.0f turn_sum=%.0f deg s=%.0f..%.0f m v=%.0f..%.0f' % (obj[-4:], *s['f'], s['t'][0], s['t'][1], s['t'][1] - s['t'][0], s['o'], s['mx'], s['end'], s['turn'], s['s0'], s['s1'], s['vmin'], s['vmax']))
Gz = -57569.0  # gate z, m (world/500), from the grid point at s = -4392
prevf = None
for l in sys.stdin:
    if f' obj={obj} ' not in l: continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
    if f < lo: continue
    fw = [float(x) for x in d['fwd'].split(',')]; nose = math.degrees(math.acos(max(-1, min(1, -fw[2]))))
    s_m = -(int(d['pos'].split(',')[2]) / 500.0 - Gz); v = int(d['v']) / 500; t = int(d['t_ms']) / 1000
    turn = math.degrees(math.acos(max(-1, min(1, sum(a * b for a, b in zip(fw, prevf)))))) if prevf else 0.0
    prevf = fw
    key = d['order']
    if nose > 30:
        if seg and seg['o'] == key:
            seg['f'][1] = f; seg['t'][1] = t; seg['mx'] = max(seg['mx'], nose); seg['end'] = nose; seg['turn'] += turn; seg['s1'] = s_m
            seg['vmin'] = min(seg['vmin'], v); seg['vmax'] = max(seg['vmax'], v)
        else:
            flush(seg); seg = {'f': [f, f], 't': [t, t], 'o': key, 'mx': nose, 'end': nose, 'turn': 0.0, 's0': s_m, 's1': s_m, 'vmin': v, 'vmax': v}
    else:
        flush(seg); seg = None
flush(seg)
