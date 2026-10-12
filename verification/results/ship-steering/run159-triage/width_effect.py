"""Run159 triage, question 8: how often the 16b lateral route (i) failed in the traced ship_capital_lane rows, and how many
(c) watch rows would have passed (i) with a narrower hull (w = k r for k in 0.45, 0.38): lat_now grows by at most
(w_A - k r_A) + (w_B - k r_B) (rho = min(h, w + len sin theta): exact when not capped at h, an upper bound otherwise).
Route (i) needs lat_gap >= 50 + mot. r per ship from the census (w / w_r).  Usage: python3 -I width_effect.py <extracted rows>"""
import re, sys
from collections import Counter
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
R = []; wr = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_capital_lane '): R.append(KV(l))
        elif l.startswith('ship_capital_census '):
            d = KV(l); wr[round(float(d['w_m']))] = float(d['r_m'])
print('census w -> r:', wr)
print('rows', len(R), 'verdict/route', dict(Counter(r['verdict'] + '/' + r['route'] for r in R)), 'watch=1', sum(r['watch'] == '1' for r in R))
W = [r for r in R if r['watch'] == '1']
print('watch rows by verdict/route', dict(Counter(r['verdict'] + '/' + r['route'] for r in W)))
c = [r for r in W if r['verdict'] == 'c']
for k in (0.45, 0.38):
    flip = 0
    for r in c:
        wa, wb = float(r['w_A']), float(r['w_B']); dA = wa - k * wr.get(round(wa), wa / 0.5); dB = wb - k * wr.get(round(wb), wb / 0.5)
        if float(r['lat_gap']) + max(0, dA) + max(0, dB) >= 50 + float(r['mot']): flip += 1
    print('w = %.2f r: (c) watch rows that would pass route (i): %d of %d' % (k, flip, len(c)))
miss = sorted(float(r['lat_gap']) - 50 - float(r['mot']) for r in c)
print('(c) watch rows: lat_gap - (50 + mot) quartiles', [round(miss[int(q * (len(miss) - 1))]) for q in (0, .25, .5, .75, 1)] if miss else '-')
