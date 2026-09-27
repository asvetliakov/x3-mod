#!/usr/bin/env python3
"""Run 351/352 far-weight A/B, log side: taa config row, camera pose at each F8 capture frame (t, forward = row 2 of r,
rotation_deg vs previous frame, |dt| per frame), and frame_end dt_ms p50 (non-capture frames, excluding the burst and
the 3 frames after it; 'stand' = frames whose camera t lies within 3 km of the first capture frame's t).
Usage: log_rows.py <session dir>..."""
import glob, re, sys, numpy as np
for d in sys.argv[1:]:
    L = glob.glob(f'{d}/session-*.log')[0]
    caps = sorted({int(re.search(r'_(\d+)\.', p).group(1)) for p in glob.glob(f'{d}/present_1_*.bgra8')})
    cam, dt, cfg = {}, {}, None
    for line in open(L, errors='replace'):
        if line.startswith('camera_state device=1 frame='):
            f = int(re.search(r'frame=(\d+)', line).group(1))
            t = re.search(r' t=([-\d.]+),([-\d.]+),([-\d.]+)', line)
            r2 = [float(re.search(rf' r2{i}=([-\d.e]+)', line).group(1)) for i in range(3)]
            rd = float(re.search(r' rotation_deg=([-\d.]+)', line).group(1))
            cam[f] = (np.array(list(map(float, t.groups()))), np.array(r2), rd)
        elif line.startswith('frame_end device=1 '):
            f = int(re.search(r'frame=(\d+)', line).group(1)); dt[f] = (float(re.search(r'dt_ms=([\d.]+)', line).group(1)), 'capture=1' in line)
        elif line.startswith('motion_output_taa ') and cfg is None:
            cfg = ' '.join(re.findall(r'(?:history_weight|far_weight|far_f0|far_f1|thin_region|far_gate|thin_gate)=\S+', line))
    print(f'== {d}\n{cfg}')
    t0 = cam[caps[0]][0]
    for f in caps:
        t, fw, rd = cam[f]; p = cam.get(f - 1)
        mv = np.linalg.norm(t - p[0]) if p else float('nan')
        print(f'frame {f}: t {t[0]:.0f},{t[1]:.0f},{t[2]:.0f} |t-t0| {np.linalg.norm(t-t0):.0f} fwd {fw[0]:+.4f},{fw[1]:+.4f},{fw[2]:+.4f} rot_deg/frame {rd:.4f} move/frame {mv:.1f}')
    excl = set()
    for f in caps:
        excl.update(range(f, f + 4))
    good = [(f, v[0]) for f, v in dt.items() if f > 300 and f not in excl and not v[1]]
    stand = [x for f, x in good if f in cam and np.linalg.norm(cam[f][0] - t0) < 3000]
    allv = [x for _, x in good]
    print(f'dt_ms p50 all(f>300) {np.median(allv):.2f} n={len(allv)} | stand p50 {np.median(stand):.2f} p90 {np.percentile(stand,90):.2f} n={len(stand)}')
