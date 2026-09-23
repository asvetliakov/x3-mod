"""Median of phase_windows.py per-window columns over win_end ranges. Usage: window_split.py OUT.txt LO:HI [LO:HI...]"""
import sys, statistics as st
rows = [l.split() for l in open(sys.argv[1]) if l[:1].isdigit() and 'timing.' not in l]
cols = 'dt_ms draws sel_us sdepth sunapp retain hdr_wb meter hdr_rb taa fill fog proxy_sum_ms'.split()
hdr = 'win_end nfr dt_ms draws sel_us c4 fpr4 sdepth sunapp retain hdr_wb meter hdr_rb taa fill fog fogon proxy_sum_ms'.split()
for rg in sys.argv[2:]:
    lo, hi = map(int, rg.split(':')); sel = [r for r in rows if lo <= int(r[0]) <= hi and r[hdr.index('hdr_wb')] not in ('0', '-')]
    out = []
    for c in cols:
        v = [float(r[hdr.index(c)]) for r in sel if r[hdr.index(c)] != '-']
        out.append(f'{c}={st.median(v):g}' if v else f'{c}=-')
    print(rg, 'windows', len(sel), ' '.join(out))
