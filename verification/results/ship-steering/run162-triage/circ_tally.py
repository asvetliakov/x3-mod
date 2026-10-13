"""Run161 Q4: tally of circ_cause.txt (small circlers, >= 360 deg in 60 s): engine warns present and the warn others' classes.
Usage: python3 -I circ_tally.py circ_cause.txt"""
import re, sys, collections as C
c = C.Counter(); n = nw = 0
for l in open(sys.argv[1]):
    w = re.search(r'warns (\[.*\]|-)', l)
    if not w: continue
    n += 1
    if w[1] == '-': c['no warn (detour/avoidance or untraced)'] += 1; continue
    nw += 1
    others = re.findall(r"'\d+:(\d+)/([^:]+):", w[1])
    c['+'.join(sorted(set('gate' if a == '18' else ('station' if a in ('5', '6') else b) for a, b in others)))] += 1
print(sys.argv[1], 'circlers', n, 'with engine warns', nw, dict(c))
