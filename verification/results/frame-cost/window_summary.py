#!/usr/bin/env python3
"""Median over 300-frame windows (from window_rows.py output) of the proxy's CPU-side shares, split menu
(draws_p50 >= 490) vs flight: draw-hook overhead = draw_p50_us - draw_native_p50_us (per frame and per draw),
scene_p50_us (EndScene hook incl. the proxy post chain CPU submission), gap_pre / gap_post, dt.
Usage: window_summary.py windows-runN.txt"""
import sys
rows = [l.split() for l in open(sys.argv[1]) if l[0].isdigit()]
H = open(sys.argv[1]).readline().split()
def col(r, name): return float(r[H.index(name)])
out = {'menu': [], 'flight': []}
for r in rows[1:]:  # skip the first (startup) window
    d = col(r, 'draws_p50'); k = 'menu' if d >= 490 else 'flight'
    out[k].append(r)
def med(v): v = sorted(v); return v[len(v) // 2] if v else float('nan')
print(sys.argv[1])
for k, rs in out.items():
    if not rs: continue
    hook = [col(r, 'draw_p50_us') - col(r, 'draw_native_p50_us') for r in rs]
    per = [(col(r, 'draw_p50_us') - col(r, 'draw_native_p50_us')) / col(r, 'draws_p50') for r in rs]
    print('%-6s windows=%3d dt_p50=%6.0f draws=%4.0f hook_overhead_us=%5.0f (%.2f us/draw) native_draw_us=%5.0f scene_us=%5.0f gap_pre_us=%5.0f gap_post_us=%4.0f present_us=%3.0f' % (
        k, len(rs), med([col(r, 'dt_p50_us') for r in rs]), med([col(r, 'draws_p50') for r in rs]), med(hook), med(per),
        med([col(r, 'draw_native_p50_us') for r in rs]), med([col(r, 'scene_p50_us') for r in rs]), med([col(r, 'gap_pre_p50_us') for r in rs]),
        med([col(r, 'gap_post_p50_us') for r in rs]), med([col(r, 'present_p50_us') for r in rs])))
