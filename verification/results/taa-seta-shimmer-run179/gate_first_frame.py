#!/usr/bin/env python3
"""Gate and lock state of one object's pixels in the FIRST frame of a burst, the only capture frame whose motion is an
in-flight step (it follows an ordinary frame). Object pixels from its object_bounds rows (node). Camera openness from the
stored peak-hold pair of the taa_age dump (openC <= 1 - q/4, resolve.hlsl:181 / closureHold), lock lifetime from taa_lock
(b & 31), the region hold from the age fraction, farw from the logged far band, and the expected openness of the logged
speed gate at the pixel's screen step. Usage: gate_first_frame.py <capdir> <log> <frame> <node>"""
import sys, re, subprocess, numpy as np
W, H, L = 5120, 1440, 8
cap, log, f, node = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
g = lambda pat: subprocess.run(['grep', '-m400', '-E', pat, log], capture_output=True, text=True).stdout.splitlines()
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
t = kv(g('^motion_output_taa device=1 initialize')[0]); lo, hi, f0b, f1b = (float(t[k]) for k in ('far_speed_lo', 'far_speed_hi', 'far_f0', 'far_f1'))
cs = kv(g(f'^camera_state device=1 frame={f} ')[0]); p00, p22, p32 = float(cs['p00']), float(cs['p22']), float(cs['p32'])
mo = kv(g(f'^motion_output_frame device=1 frame={f} ')[0]); jx, jy = float(mo['jitter_x']), float(mo['jitter_y'])
dep = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
mot = np.fromfile(f'{cap}/motion_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
ys, xs = np.mgrid[0:H, 0:W]
sel = np.zeros((H, W), bool); nd = 0
for l in g(f'^object_bounds device=1 frame={f} .*node={node} '):
    k = kv(l); nd += 1
    sel |= (xs + .5 >= float(k['sx0'])) & (xs + .5 <= float(k['sx1'])) & (ys + .5 >= float(k['sy0'])) & (ys + .5 <= float(k['sy1'])) \
           & (dep[..., 0] >= float(k['zmin']) - 2e-6) & (dep[..., 0] <= float(k['zmax']) + 2e-6)
sel &= (mot[..., 3] == 1) & (dep[..., 0] >= 0) & (dep[..., 0] < 1)
s = np.hypot(mot[..., 0] * W - (xs + .5) + jx, mot[..., 1] * H - (ys + .5) + jy)[sel]
age = np.abs(np.fromfile(f'{cap}/taa_age_1_{f}.r32f', np.float32).reshape(H, W)[sel])
fr = np.where(age <= 65, np.round((age - np.floor(age)) * 65536), 0)
hold = fr % 128; q = np.floor((np.floor(fr / 128) + .5) / (L + 1)); openC = 1 - q / 4
life = np.fromfile(f'{cap}/taa_lock_1_{f}.bgra8', np.uint8).reshape(H, W, 4)[sel][:, 0] & 31
scale = p00 * W * .5; d0, d1 = p22 + p32 / (f0b * scale), p22 + p32 / (f1b * scale)
farw = np.clip((dep[..., 0][sel] - d0) / (d1 - d0), 0, 1)
exp_open = np.clip(1 - (s - lo) / (hi - lo), 0, 1)
n = sel.sum()
print(f'frame {f} node {node}: {nd} draws, {n} px, view z p50 {np.median(dep[..., 2][sel]):.0f}; gate lo/hi {lo}/{hi}')
print(f'  screen step px/f p50 {np.median(s):.3f} p90 {np.percentile(s, 90):.3f}; expected own openness at that step p50 {np.median(exp_open):.2f}, >=0.5 {np.mean(exp_open >= .5):.3f}')
print(f'  age-dump camera openness bound: openC>0.5 {np.mean(openC > .5):.3f}, =1 {np.mean(openC >= 1):.3f}, q hist {np.bincount(q.astype(int), minlength=5)[:5].tolist()}')
print(f'  farw>0 {np.mean(farw > 0):.3f}; region hold>0 {np.mean(hold > 0):.3f}; locked (t>0) {np.mean(life > 0):.3f}; locked AND openC>0.5 {np.mean((life > 0) & (openC > .5)):.3f}; '
      f'(farw>0 or region) AND openC>0.5 {np.mean(((farw > 0) | (hold > 0)) & (openC > .5)):.3f}')
