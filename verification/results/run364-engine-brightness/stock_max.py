#!/usr/bin/env python3
"""Stock-tree bursts (1920x1080): per run, the first hdr frame's engine-space max channel, count > 1 and > 1+eps,
and the count of in-scene DEFAULT-pair screen draws (ONE/INVSRCCOLOR) in that frame from log_draws.py."""
import glob, os, re, subprocess, numpy as np
for d in sorted(glob.glob('/tmp/x3-bottleX3-run35[1-6]')):
    f = sorted(glob.glob(f'{d}/hdr_1_*.rgba16f'))[0]; fr = re.search(r'_(\d+)\.rgba16f', f).group(1)
    c = np.fromfile(f, np.float16).reshape(1080, 1920, 4)[..., :3].astype(np.float32); mx = c.max(-1)
    L = glob.glob(f'{d}/session-*.log')[0]
    out = subprocess.run(['python3', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'log_draws.py'), L, fr, 'd5e1c753'], capture_output=True, text=True).stdout
    rows = [l.split() for l in out.splitlines() if l[:1].isdigit()]
    scr = sum(1 for r in rows if r[8] == '4'); add_in = sum(1 for r in rows if r[8] == '2' and r[5] == '1'); lens = sum(1 for r in rows if r[8] == '2' and r[5] == '0')
    print(f'{os.path.basename(d)} frame {fr}: max {mx.max():.3f} px>1 {(mx > 1.001).sum()} | DEFAULT pair draws: screen {scr}, ONE/ONE z-on {add_in}, ONE/ONE z-off (lens) {lens}')
