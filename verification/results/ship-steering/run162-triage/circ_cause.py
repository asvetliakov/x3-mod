# Run160b Q6: for each small circler (circling_small.txt, worst-60-s turn >= 360 deg) the engine warn rows of that ship inside
# its window (other id, class/code, the other's order at warn_pre), and its 0x14 leader's order mix in the window (E rows).
# Usage: python3 -I circ_cause.py rows.txt circling_small.txt
import sys, re, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
W = []
for l in open(sys.argv[2]):
    m = re.match(r'(\S+) (\S+) total_turn .* frames (\d+)-(\d+) .* turn (\d+) deg .* tgts (\{[^}]*\})', l)
    if m and int(m[5]) >= 360: W.append((m[1], m[2], int(m[3]), int(m[4]), int(m[5]), m[6]))
warn = C.defaultdict(list); pre = {}; Lo = C.defaultdict(C.Counter)
leaders = {}
for w in W:
    t = eval(w[5]); ld = [k for k in t if k in caps]; leaders[w[0]] = ld[0] if ld else None
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); warn[d['obj']].append(d)
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); pre[(d['obj'], int(d['frame']))] = l.split('e_verdict=')[1].strip()
    elif l.startswith('ship_avoid_e_verdict '):
        m = re.search(r' A=(\S+) A_order=(\S+) .* frame=', l)
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        for w in W:
            if leaders[w[0]] in (d['A'], d['B']) and w[2] <= int(d['frame']) <= w[3]:
                Lo[w[0]][d['A_order'] if d['A'] == leaders[w[0]] else d['B_order']] += 1
for o, c, f0, f1, turn, tg in W:
    ws = [x for x in warn[o] if f0 - 50 <= int(x['frame']) <= f1]
    print('%s %s turn %d frames %d-%d tgts %s | leader %s orders in window %s | warns %s' % (o, c, turn, f0, f1, tg, leaders[o], dict(Lo[o]) or '-',
          ['%s:%s/%s:%s' % (x['frame'], x['class'], x['code'], x['other']) for x in ws] or '-'))
