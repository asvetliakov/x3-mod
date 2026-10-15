# Section 13.8 statistics: per cast over its runs (20 seeds x tf 1 / 6 / 10), the mode against its level-0 twin: launches, spans where
# both launched every arrival-side ship, the arrival-side ships flown 0x12 (ep), the contact episodes of a pair with one, and the 0x12 a
# write of ours (a stop or a cap) preceded within 30 s + the step (ours), and the runs with >= 3 arrival-side 0x12 (mode / twin: the kill criterion's
# count per return trip, decision A), and the runs identical to the twin (0x12 episodes, contacts, launches, span: the arrival side is vanilla). Usage: python3 -I stat_compare.py <mode lines>
import re, sys
def f(pat, s):
    m = re.search(pat, s); return float(m.group(1)) if m else -1.0
rows = {}
for fn in sys.argv[1:]:
    for l in open(fn):
        m = re.match(r'scenario C (\S+) mode tf (\d+) seed (\d+): (.*)\|\| twin: (.*)', l)
        if not m: continue
        a, t = m.group(4), m.group(5)
        r = dict(span=f(r' span ([-0-9.]+) over', a), ep=f(r'M3 ep (\d+)', a), ours=f(r' ours (\d+)', a), ct=f(r' contact (\d+)', a),
                 tspan=f(r' span ([-0-9.]+)', t), tep=f(r' ep (\d+)', t), tct=f(r' contact (\d+)', t), l=f(r' launched (\d+) mode', a),
                 tl=f(r'^launched (\d+)', t), n=f(r'\| arr (\d+)', a), last=f(r' last_launch ([-0-9.]+)', a), lastarr=f(r' last_arr ([-0-9.]+)', a))
        rows.setdefault(m.group(1), []).append(r)
for c, rr in sorted(rows.items()):
    n = len(rr)
    fewer = sum(1 for x in rr if x['l'] < x['tl'])
    allm = sum(1 for x in rr if x['l'] == x['n']); allt = sum(1 for x in rr if x['tl'] == x['n'])
    both = [x for x in rr if x['l'] == x['n'] and x['tl'] == x['n']]
    longer = sum(1 for x in both if x['span'] > x['tspan'])
    dsp = sorted(x['span'] - x['tspan'] for x in both)
    ep3 = sum(1 for x in rr if x['ep'] >= 3); tep3 = sum(1 for x in rr if x['tep'] >= 3) # (the kill criterion's >= 3 member 0x12 per return trip)
    same = sum(1 for x in rr if (x['ep'], x['ct'], x['l'], x['span']) == (x['tep'], x['tct'], x['tl'], x['tspan'])) # (the identity, section 13)
    print('%-14s runs %d | all launched mode %d twin %d | mode fewer launches %d | both launched %d: mode span longer %d, delta median %.0f p90 %.0f max %.0f | ep %d/%d contact %d/%d ours %d | runs ep>=3 %d/%d | identical to the twin %d/%d' % (
        c, n, allm, allt, fewer, len(both), longer, dsp[len(dsp)//2] if dsp else 0, dsp[int(len(dsp)*0.9)] if dsp else 0, dsp[-1] if dsp else 0,
        sum(x['ep'] for x in rr), sum(x['tep'] for x in rr), sum(x['ct'] for x in rr), sum(x['tct'] for x in rr), sum(x['ours'] for x in rr), ep3, tep3, same, n))
