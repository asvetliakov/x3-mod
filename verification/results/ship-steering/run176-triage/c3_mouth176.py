# Run176 Q2: C3's escorts against the gate, each other and the leader from site E's rate-limited ship_avoid_e_verdict rows (the C3 ships are not in
# ship_trace's 8 slots): frame, A, A_order, B, B_order, d_m, verdict, gate_row/obst_row; one row per (A, B) every >= 40 frames.
# Usage: python3 -I c3_mouth176.py ROWS LO_FRAME HI_FRAME
import sys, re
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l)); lo, hi = int(sys.argv[2]), int(sys.argv[3])
S = {'0x30edeff8', '0xa89ccf60', '0x905b2760'}; B = S | {'0x90a178e8'}; last = {}
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_avoid_e_verdict '): continue
    d = kv(l); f = int(d['frame'])
    if not lo <= f <= hi or d['A'] not in S or d['B'] not in B: continue
    k = (d['A'], d['B'])
    if k in last and f - last[k] < 40: continue
    last[k] = f
    print(f, d['A'][-4:], d['A_order'], 'B', d['B'][-4:], d['B_order'], 'd', d['d_m'], d['verdict'], d.get('gate_row', ''), d.get('obst_row', ''))
