#!/usr/bin/env python3
"""Median of frame_timing window stats inside the first background-21 (greenvoid) stay of each run.
Spans are the volumetric_fog_sector frame boundaries grepped from each log. Reads frame_timing_runNNN.txt."""
import re, statistics as st
spans = {'287': (582, 20915), '277': (2303, 19386), '281': (1186, 41444)}
for r, (a, b) in spans.items():
    sel = [dict(re.findall(r'(\w+)=([\d.]+)', l)) for l in open(f'frame_timing_run{r}.txt') if l.startswith('frame=')]
    sel = [g for g in sel if a + 300 <= int(g['frame']) <= b]
    print(f"run{r} frames {a}..{b} windows={len(sel)} dt_p50_med={st.median(int(g['dt_p50']) for g in sel)} dt_p95_med={st.median(int(g['dt_p95']) for g in sel)} draws_p50_med={st.median(int(g['draws_p50']) for g in sel)} draw_p50_us_med={st.median(int(g['draw_p50_us']) for g in sel)}")
