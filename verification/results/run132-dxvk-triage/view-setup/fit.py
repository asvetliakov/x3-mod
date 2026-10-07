"""From slow_join_run*.txt: least-squares view_setup_us = a + b*lock_us and the median of view_setup - lock. Usage: fit.py <slow_join.txt>"""
import sys, statistics as st
rows = [l.split() for l in open(sys.argv[1]) if l[0].isdigit()]
p = [(float(r[1]), float(r[2])) for r in rows if r[2] != 'None']
x = [b for a, b in p]; y = [a for a, b in p]; mx, my = st.mean(x), st.mean(y)
b = sum((xi-mx)*(yi-my) for xi, yi in zip(x, y)) / sum((xi-mx)**2 for xi in x); a = my - b*mx
print(f'n={len(p)} slope={b:.3f} intercept_us={a:.0f} lock_p50={st.median(x):.0f} view_setup_p50={st.median(y):.0f} residual(view_setup-lock)_p50={st.median(yi-xi for xi, yi in zip(x, y)):.0f}')
