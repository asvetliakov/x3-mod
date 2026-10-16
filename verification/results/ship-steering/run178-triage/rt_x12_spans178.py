# Run178 Q2: each 0x12 episode of the four (from the first engine warn row) with its span bounded by rows: the last row carrying
# order 0x12 for the ship (trace order, site E A_order, warn rows) and the first later row carrying another order.
# Usage: python3 -I rt_x12_spans178.py ROWS
import sys, re, bisect
S = ['0x8fe612b8', '0x8fe58420', '0x39cfa520', '0x39cf2180']
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
f2t = {}; od = {s: [] for s in S}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        d = kv(l); f = int(d['frame']); f2t.setdefault(f, int(d['t_ms']) / 1e3)
        if d['obj'] in od: od[d['obj']].append((f, d['order']))
    elif l.startswith('ship_avoid_e_verdict '):
        d = kv(l)
        if d['A'] in od: od[d['A']].append((int(d['frame']), d['A_order']))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in od: od[d['obj']].append((int(d['frame']), d['order'], d['other']))
fk = sorted(f2t); T = lambda f: f2t[fk[min(bisect.bisect_left(fk, f), len(fk) - 1)]]
tot = 0; n = 0
for s in S:
    seq = sorted(od[s]); i = 0
    while i < len(seq):
        if seq[i][1] == '0x12' and (i == 0 or seq[i - 1][1] != '0x12'):
            j = i
            while j + 1 < len(seq) and seq[j + 1][1] == '0x12': j += 1
            w = [x for x in seq[i:j + 1] if len(x) == 3]
            if not w: i = j + 1; continue
            seq[i] = (seq[i][0], seq[i][1], w[0][2])
            nx = seq[j + 1] if j + 1 < len(seq) else None
            t0, tl = T(seq[i][0]), T(seq[j][0]); te = T(nx[0]) if nx else None
            n += 1; tot += (te or tl) - t0
            print('%s 0x12 vs %s from %.1f: last 0x12 row %.1f (+%.1f s), next order %s at %.1f (+%.1f s), rows %d' % (
                s[-4:], seq[i][2][-4:], t0, tl, tl - t0, nx[1] if nx else '-', te or 0, (te or tl) - t0, j - i + 1))
            i = j + 1
        else: i += 1
print('episodes %d, total %.1f s' % (n, tot))
