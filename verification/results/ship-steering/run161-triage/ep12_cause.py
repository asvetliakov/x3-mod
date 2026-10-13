"""Run161 Q1: capital 0x12 episodes (episodes12.txt) by cause: arrival side (phase 'arrival'), vs capital (other class 7 with a
capital code), vs small (other class 7 otherwise), vs gate (class 18), vs station (class 5/6), no warn row. Game s and count.
Usage: python3 -I ep12_cause.py episodes12.txt"""
import sys, re, collections as C
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
t = C.Counter(); n = C.Counter()
for l in open(sys.argv[1]):
    p = [x.strip() for x in l.split('|')]
    if len(p) < 5 or not p[0].startswith('0x'): continue
    dur = float(p[1].split()[2]); ph = p[3]; w = p[4].split()
    if ph.startswith('arrival'): k = 'arrival side'
    elif w[0] == 'no' or len(w) < 2 or '/' not in w[1]: k = 'no warn row / position only'
    else:
        cl, code = w[1].split('/')
        k = 'vs gate' if cl == '18' else 'vs station' if cl in ('5', '6') else 'vs capital' if code in CAP else 'vs small' if cl == '7' else 'vs class' + cl
    k += ' (%s)' % ('exit-approach' if ph.startswith('exit') else 'escort/transit' if ph.startswith('escort') else 'arrival') if not k.startswith('arrival') else ''
    t[k] += dur; n[k] += 1
print(sys.argv[1], {k: '%.1f s / %d' % (t[k], n[k]) for k in sorted(t)}, 'total %.1f s / %d' % (sum(t.values()), sum(n.values())))
