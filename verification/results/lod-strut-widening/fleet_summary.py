"""Category split of the sub-pixel geometry census (fleet_census.json from thin_geometry_census.py):
per category the bodies over 1/5/10 % of record area on sub-pixel patches at s = T_pad (5120x1440), the strut
(non-bevel) share, faces and patches; and the fleet distribution of the per-body median sub-pixel patch width."""
import json, statistics, sys
rows = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'fleet_census.json'))
ok = [r for r in rows if 'error' not in r]
def c(r, d='5120x1440'): return r[f's{r["t_pad"]}@{d}']
for cat in ('stations/', 'ships/', 'others/'):
    rr = [r for r in ok if r['name'].startswith(cat)]
    a1 = sum(1 for r in rr if c(r)['feat_area'] > 0.01); a5 = sum(1 for r in rr if c(r)['feat_area'] > 0.05); a10 = sum(1 for r in rr if c(r)['feat_area'] > 0.10)
    s1 = sum(1 for r in rr if c(r)['strut_area'] > 0.01); s5 = sum(1 for r in rr if c(r)['strut_area'] > 0.05)
    fa = sum(c(r)['feat'] for r in rr); ff = sum(r['faces'] for r in rr)
    tp = sum(c(r)['thin_patches'] for r in rr); pp = sum(c(r)['patches'] for r in rr); bp = sum(c(r)['bevel_patches'] for r in rr)
    med = statistics.median(c(r)['feat_area'] for r in rr) if rr else 0
    meds = statistics.median(c(r)['strut_area'] for r in rr) if rr else 0
    print(f'{cat:10s} bodies {len(rr):3d} feat_area>1% {a1:3d} >5% {a5:3d} >10% {a10:3d} median feat_area {100*med:.2f}% strut_area>1% {s1} >5% {s5} median strut_area {100*meds:.2f}%'
          f' faces on sub-px patches {fa}/{ff} ({100*fa/max(1,ff):.1f}%) patches sub-px {tp}/{pp} ({100*tp/max(1,pp):.1f}%) of which bevel {bp}')
w = [c(r)['feat_h_p50'] for r in ok if c(r)['feat_h_p50']]
q = statistics.quantiles(w, n=10)
print('per-body median sub-px patch width at s=T_pad (px), fleet p10/p50/p90:', [round(q[i], 2) for i in (0, 4, 8)])
print('census seconds total', round(sum(r['seconds'] for r in ok)), 'max per body', round(max(r['seconds'] for r in ok), 1))
