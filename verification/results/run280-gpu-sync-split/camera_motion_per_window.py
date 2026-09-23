"""Per 300-frame window: camera translation/rotation from camera_state rows (run280)."""
import re, sys, math, statistics as st
log = sys.argv[1]
rows = {}
for line in open(log, errors='replace'):
    if not line.startswith('camera_state device='): continue
    f = int(re.search(r' frame=(\d+)', line).group(1))
    v = re.search(r' valid=(\d)', line).group(1)
    t = re.search(r' t=([-\d.e]+),([-\d.e]+),([-\d.e]+)', line)
    R = [float(re.search(r' r%d%d=([-\d.e]+)' % (i, j), line).group(1)) for i in range(3) for j in range(3)]
    cut = re.search(r' camera_cut=(\d)', line)
    rows[f] = (v == '1', tuple(map(float, t.groups())), R, int(cut.group(1)))
prev = None
win = {}
for f in sorted(rows):
    valid, t, R, cut = rows[f]
    w = f // 300 + 1
    d = win.setdefault(w, {'valid': 0, 'n': 0, 'step': [], 'rot': [], 'cut': 0})
    d['n'] += 1
    if valid:
        d['valid'] += 1; d['cut'] += cut
        if prev and prev[0]:
            d['step'].append(math.dist(t, prev[1]))
            tr = sum(R[3*i+k]*prev[2][3*i+k] for i in range(3) for k in range(3))
            d['rot'].append(math.degrees(math.acos(max(-1, min(1, (tr-1)/2)))))
    prev = (valid, t, R)
def q(a, p):
    a = sorted(a); return a[min(len(a)-1, int(p*len(a)))] if a else float('nan')
print('win frames valid cuts step_med step_p90 step_sum rotdeg_per_frame_med p90 (from r matrix delta) still_frames(step<1e-3&rot<0.01)')
for w, d in sorted(win.items()):
    still = sum(1 for s, r in zip(d['step'], d['rot']) if s < 1e-3 and r < 0.01)
    print(f"W{w} {d['n']} {d['valid']} {d['cut']} {q(d['step'],.5):.3f} {q(d['step'],.9):.3f} {sum(d['step']):.1f} {q(d['rot'],.5):.4f} {q(d['rot'],.9):.4f} {still}")
