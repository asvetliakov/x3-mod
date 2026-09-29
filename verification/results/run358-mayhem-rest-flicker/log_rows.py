#!/usr/bin/env python3
"""Run 358 (Mayhem 3, 0.7.0) log side: per capture frame camera t, rotation_deg, move/frame, jitter index, draws/routed;
frame_end dt_ms stats in windows around each burst (non-capture frames, 60 frames before / 60 after, excluding burst+3);
lod_switch rows inside burst windows. Usage: log_rows.py <dir>"""
import glob, re, sys, numpy as np
d = sys.argv[1]; L = glob.glob(f'{d}/session-*.log')[0]
caps = sorted({int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/hdr_1_*.rgba16f')})
bursts = []
for f in caps:
    if bursts and f == bursts[-1][-1] + 1: bursts[-1].append(f)
    else: bursts.append([f])
cam, dt, mof, lods = {}, {}, {}, []
for line in open(L, errors='replace'):
    if line.startswith('camera_state device=1 frame='):
        f = int(re.search(r'frame=(\d+)', line).group(1)); t = re.search(r' t=([-\d.]+),([-\d.]+),([-\d.]+)', line)
        r2 = [float(re.search(rf' r2{i}=([-\d.e]+)', line).group(1)) for i in range(3)]
        cam[f] = (np.array(list(map(float, t.groups()))), np.array(r2), float(re.search(r' rotation_deg=([-\d.]+)', line).group(1)))
    elif line.startswith('frame_end device=1 '):
        f = int(re.search(r'frame=(\d+)', line).group(1)); dt[f] = (float(re.search(r'dt_ms=([\d.]+)', line).group(1)), 'capture=1' in line)
    elif line.startswith('motion_output_frame device=1 '):
        f = int(re.search(r'frame=(\d+)', line).group(1))
        g = lambda k: re.search(rf' {k}=(\S+)', line).group(1)
        mof[f] = (g('draws'), g('routed'), g('jitter_index'), g('taa_resolved'), g('taa_history'), g('camera_cut'))
    elif line.startswith('lod_switch '):
        lods.append(line.split()[1:6])
for b in bursts:
    print(f'== burst {b[0]}-{b[-1]} ({len(b)} frames)')
    t0 = cam[b[0]][0]
    for f in b:
        t, fw, rd = cam[f]; p = cam.get(f - 1); mv = np.linalg.norm(t - p[0]) if p else float('nan')
        m = mof.get(f, ('?',) * 6)
        print(f'frame {f}: t {t[0]:.2f},{t[1]:.2f},{t[2]:.2f} |t-t0| {np.linalg.norm(t-t0):.3f} fwd {fw[0]:+.5f},{fw[1]:+.5f},{fw[2]:+.5f} rot_deg {rd:.4f} move {mv:.3f} | draws {m[0]} routed {m[1]} jit {m[2]} resolved {m[3]} hist {m[4]} cut {m[5]}')
    ex = set(range(b[0], b[-1] + 4))
    for name, rng in (('before', range(b[0] - 60, b[0])), ('after', range(b[-1] + 4, b[-1] + 64))):
        v = [dt[f][0] for f in rng if f in dt and f not in ex and not dt[f][1]]
        mv = [np.linalg.norm(cam[f][0] - cam[f - 1][0]) for f in rng if f in cam and f - 1 in cam]
        rot = [cam[f][2] for f in rng if f in cam]
        if v: print(f'  {name}: dt_ms p50 {np.median(v):.1f} p90 {np.percentile(v,90):.1f} max {max(v):.1f} fps(p50) {1000/np.median(v):.1f} n={len(v)}; move/frame max {max(mv):.3f}; rot_deg max {max(rot):.4f}')
    inb = [r for r in lods if b[0] - 60 <= int(r[0].split('=')[1]) <= b[-1] + 60]
    print(f'  lod_switch rows within burst +-60 frames: {len(inb)} {inb}')
v = [x for f, (x, c) in dt.items() if f > 300 and not c]
print(f'session dt_ms p50 {np.median(v):.1f} p90 {np.percentile(v,90):.1f} (f>300, non-capture, n={len(v)})')
