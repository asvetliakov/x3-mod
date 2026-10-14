# Run167 Q2: hold_wait.txt "== ALL capitals" collapsed to the brief's reason classes (game s, real s). Usage: python3 -I hold_totals.py hold_wait.txt
import sys, re, collections as C
T = C.defaultdict(lambda: [0.0, 0.0]); on = False
for l in open(sys.argv[1]):
    if l.startswith('== ALL'): on = True; continue
    if on and l.startswith('EXCESS'): break
    if not on: continue
    m = re.match(r'\s*([\d.]+)\s+([\d.]+)\s+(.*)', l)
    if not m: continue
    r = m[3]
    k = ('escort waiting for order 4, leader launched' if '(leader gate -)' in r else 'escort waiting for order 4, leader not launched' if r.startswith('escort') else
         r if r.startswith('funnel busy') or r.startswith('head') else 'other: ' + r)
    T[k][0] += float(m[1]); T[k][1] += float(m[2])
for k, (g, r) in sorted(T.items(), key=lambda x: -x[1][0]): print('%8.1f %7.1f  %s' % (g, r, k))
print('%8.1f %7.1f  total' % (sum(v[0] for v in T.values()), sum(v[1] for v in T.values())))
