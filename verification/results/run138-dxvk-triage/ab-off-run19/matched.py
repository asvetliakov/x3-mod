#!/usr/bin/env python3
"""Median view_submit/views/dt p50 over frame_phases windows with mean app draws in [237,262], from windows_by_draws_out.txt."""
import re, statistics as st
run = None; R = {}
for l in open('windows_by_draws_out.txt'):
    if l.startswith('/tmp'): run = l.split(':')[0][-5:]; continue
    d = dict((k, float(v)) for k, v in re.findall(r'(\w+)=\s*([0-9.]+)', l))
    if 237 <= d['draws'] <= 262: R.setdefault(run, []).append(d)
for r, w in R.items():
    print(r, 'windows=%d' % len(w), ' '.join('%s=%.0f' % (k, st.median(x[k] for x in w)) for k in ('draws', 'issued', 'view_submit_p50', 'views_p50', 'dt_p50')))
