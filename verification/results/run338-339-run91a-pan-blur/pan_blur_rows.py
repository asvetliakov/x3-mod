#!/usr/bin/env python3
"""Run 91 A station blur under pan: per-frame TAA / motion / camera statistics from a session log.

Usage: pan_blur_rows.py <session.log> [capture frames comma list]
Prints only summaries: rotation distribution, cut counts, routed share, TAA history share,
and the rows of the listed frames. No large output.
"""
import re, sys, statistics as st

KV = re.compile(r'(\w+)=(\S+)')
log = sys.argv[1]
caps = set(int(x) for x in sys.argv[2].split(',')) if len(sys.argv) > 2 else set()
frames = {}
cams = {}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('motion_output_frame '):
            d = dict(KV.findall(line))
            frames[int(d['frame'])] = d
        elif line.startswith('camera_state device='):
            d = dict(KV.findall(line))
            cams[int(d['frame'])] = d

def f(d, k):
    try:
        return float(d.get(k, 'nan'))
    except ValueError:
        return float('nan')

active = [d for d in frames.values() if f(d, 'taa_resolved') > 0]
print('frames', len(frames), 'taa_resolved_frames', len(active))
rot = [f(d, 'camera_rotation_deg') for d in active]
cut = sum(1 for d in active if f(d, 'camera_cut') > 0)
mcut = sum(1 for d in active if f(d, 'cut') > 0)
hist = sum(1 for d in active if f(d, 'taa_history') > 0)
print('camera_cut_frames', cut, 'motion_cut_frames', mcut, 'taa_history_frames', hist)
if rot:
    rs = sorted(rot)
    q = lambda p: rs[min(len(rs) - 1, int(p * len(rs)))]
    print('rotation_deg_per_frame p50 %.4f p90 %.4f p99 %.4f max %.4f' % (q(.5), q(.9), q(.99), rs[-1]))
    for lo, hi in ((0, 0.01), (0.01, 0.05), (0.05, 0.2), (0.2, 1), (1, 5), (5, 1e9)):
        print('  rot [%g,%g) frames %d' % (lo, hi, sum(1 for r in rot if lo <= r < hi)))
dr = [f(d, 'draws') for d in active]
ro = [f(d, 'routed') for d in active]
ma = [f(d, 'matched') for d in active]
share = [r / x for r, x in zip(ro, dr) if x > 0]
print('draws median %.0f routed median %.0f matched median %.0f routed/draws median %.3f min %.3f' % (
    st.median(dr), st.median(ro), st.median(ma), st.median(share), min(share)))
um = [f(d, 'unjittered_depth_writers') for d in active]
print('unjittered_depth_writers median %.0f max %.0f' % (st.median(um), max(um)))
w = sorted(set(d.get('taa_weight') for d in active))
print('taa_weight values', w)
for fr in sorted(caps):
    d = frames.get(fr, {}); c = cams.get(fr, {})
    print('frame %d rot=%s cam_cut=%s cut=%s draws=%s routed=%s matched=%s taa_history=%s p00=%s p11=%s t=%s r02=%s r20=%s' % (
        fr, d.get('camera_rotation_deg'), d.get('camera_cut'), d.get('cut'), d.get('draws'), d.get('routed'),
        d.get('matched'), d.get('taa_history'), c.get('p00'), c.get('p11'), c.get('t'), c.get('r02'), c.get('r20')))
