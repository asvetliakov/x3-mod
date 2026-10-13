# Run164 Q3: for each eviction, the last site-E rows of the evictee against the 0x12's other (A_order, A_age_ms, B_order, B_leader, d_m,
# verdict) and the last warn_pre rows. Usage: python3 -I evict_fresh.py rows.txt
import sys, re
Q = [('0x8e03e7e0', '0x8e05a4e0', 19300, 19486), ('0x8e049da0', '0x8e03e7e0', 24300, 24562), ('0x8e043bf8', '0x8e0388d0', 49700, 49851),
     ('0xaa68cfb0', '0x8e037070', 59300, 59890), ('0xaa68cfb0', '0x8e037070', 60500, 60602), ('0xaa68cfb0', '0xab1b7508', 66700, 66845)]
R = {q: [] for q in Q}; P = {q: [] for q in Q}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        for q in Q:
            if d['A'] == q[0] and d['B'] == q[1] and q[2] <= f <= q[3]: R[q].append((f, d['A_order'], d['A_age_ms'], d['B_order'], d['B_leader'], d['d_m'], d['verdict']))
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = dict(kv.split('=', 1) for kv in l.split()[1:] if '=' in kv); f = int(d['frame'])
        for q in Q:
            if d['obj'] == q[0] and q[2] <= f <= q[3]: P[q].append((f, d['order'], list(d.items())[-1][1]))
for q in Q:
    print('==', q, 'E rows', len(R[q]))
    for x in R[q][-4:]: print('   E', x)
    for x in P[q][-2:]: print('   pre', x)
