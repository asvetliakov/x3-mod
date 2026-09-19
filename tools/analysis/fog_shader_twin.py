#!/usr/bin/env python3
"""Offline cross-check of the stage 1 fog shaders' law against the stage 0 mock on one --taa-debug dump frame.

usage: fog_shader_twin.py <dump dir> <frame> [--tau 0.02] [--g 0.3] [--radius 10000] [--cycle 8]

Two laws over the same inputs (tools/analysis/fog_offline_mock.py loads them the same way; its loader is repeated here because
the mock is a script, not a module):
  mock    the mock's: all five cascades, float F, albedo = the hue of the mean of ALL sky pixels (--albedo-mix 1), 16 steps.
  shader  src/fog/*.hlsl: the apply slots 1-3 only (slot 0 = the own-ship map is not marched), F stored in 8 bits, the sky hue
          from the 96 x 96 sparse taps of fog_sky_level0_ps.hlsl (linear clamped at 4), the half grid ceil(W/2) x ceil(H/2).
Both use the mock's image-estimated E_sun with a white sun: the dump does not carry the light node's colour words, so the tracked
E_sun of the live pass is NOT part of this check.  Prints the mock-law aggregates (to be compared with the mock's own printout)
and the shader-vs-mock differences of F, of the hue and of the in-scatter term.  Aggregates only."""
import argparse, glob, re, subprocess
import numpy as np

ap = argparse.ArgumentParser(); ap.add_argument('dump'); ap.add_argument('frame', type=int)
ap.add_argument('--tau', type=float, default=.02); ap.add_argument('--g', type=float, default=.3); ap.add_argument('--radius', type=float, default=10000.); ap.add_argument('--cycle', type=int, default=8)
a = ap.parse_args(); D = a.dump.rstrip('/') + '/'; F = a.frame; log = glob.glob(D + 'session-*.log')[0]; STEPS = 16
def grep(kind, first=True): return subprocess.run(['grep'] + (['-m1'] if first else []) + ['-E', r'^%s device=1 frame=%d ' % (kind, F), log], capture_output=True, text=True).stdout
def f32(h): return float(np.array([int(h, 16)], np.uint32).view(np.float32)[0])
kv = dict(re.findall(r'(\w+)=([-\w.+,]+)', grep('sun_shadow_apply_params')))
if 'width' in kv:
    W, H = int(kv['width']), int(kv['height']); m00, m11, m20, m21, m22, m32 = (float(kv[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32')); margin = float(kv['margin'])
    rows = [np.array(kv['rows%d' % i].split(','), np.float64).reshape(3, 4) for i in range(5)]
    valid = [kv['valid%d' % i] == '1' for i in range(5)]; size = [int(kv['map%d' % i]) for i in range(5)]; bias = [float(kv['bias_max%d' % i]) for i in range(5)]
else:  # sun lane refused: the mock's fallback (rows from shadow_replay_map_basis and the sector camera's logged matrices)
    W, H = (int(x) for x in re.search(r'target=(\d+)x(\d+)', grep('hdr_frame')).groups())
    txt = subprocess.run(['grep', '-A60', '-m400', '-E', r'^object_context device=1 frame=%d ' % F, log], capture_output=True, text=True).stdout; blocks = txt.split('object_context '); cam = None
    for b in blocks:
        m = re.search(r'object_fade .*camera=(\w+) .*flags270=(\w+)', b)
        if m and int(m.group(2), 16) & 0x10000: cam = m.group(1); break
    Vm = Pm = None
    for b in blocks:
        if cam and re.match(r'.*? camera=%s ' % cam, b.split('\n')[0]):
            M = {r: [[f32(x) for x in bits.split(',')] for role, bits in re.findall(r'object_matrix role=(\w+) row=\d bits=(\S+)', b) if role == r] for r in ('view', 'projection')}
            if len(M['view']) == 4 and len(M['projection']) == 4: Vm, Pm = np.array(M['view']), np.array(M['projection']); break
    if Vm is None: raise SystemExit('no sector-camera draw with matrices in frame %d' % F)
    m00, m11, m20, m21, m22, m32 = Pm[0, 0], Pm[1, 1], 0., 0., Pm[2, 2], Pm[3, 2]; margin = 1.; Rv, tv = Vm[:3, :3], Vm[3, :3]
    rows, valid, size, bias = [np.zeros((3, 4))] * 5, [False] * 5, [0] * 5, [0.] * 5
    for l in grep('shadow_replay_map_basis', first=False).splitlines():
        b = dict(re.findall(r'(\w+)=([-\w.+,]+)', l)); i = int(b['cascade']); ax = [np.array(b[k].split(','), np.float64) for k in ('right', 'up', 'forward')]; c = np.array(b['center'].split(','), np.float64)
        E_, Ld, Bd = float(b['extent']), float(b['depth_light']), float(b['depth_behind']); sc = [1 / E_, 1 / E_, 1 / (Ld + Bd)]; off = [0., 0., Ld / (Ld + Bd)]
        rows[i] = np.array([np.concatenate([(Rv.T @ ax[k]) * sc[k], [(-(tv @ Rv.T) - c) @ ax[k] * sc[k] + off[k]]]) for k in range(3)])
        valid[i] = b['valid'] == '1'; size[i] = int(b['size']); bias[i] = 3. * (2 * E_ / size[i]) / (Ld + Bd)
maps = [np.fromfile(D + 'shadow_map%d_1_%d.r32f' % (i, F), np.float32).reshape(size[i], size[i]) if valid[i] else None for i in range(5)]
sun = -rows[0][2, :3] / np.linalg.norm(rows[0][2, :3])
ds = np.fromfile(D + 'depth_1_%d.rgba32f' % F, np.float32).reshape(H, W, 4).astype(np.float64)
geo = (ds[..., 0] >= 0) & (ds[..., 0] <= 1)
with np.errstate(all='ignore'):
    z = np.where(geo, np.where(ds[..., 2] > 0, ds[..., 2], m32 / (ds[..., 0] - m22)), np.inf)
eng = np.nan_to_num(np.fromfile(D + 'taa_1_%d.rgba16f' % F, np.float16).reshape(H, W, 4)[..., :3].astype(np.float64), nan=0, posinf=65504, neginf=0)
lin = np.clip(eng, 0, 65504) ** 2.2
ys, xs = np.mgrid[0:H, 0:W]
ray = np.stack([((2 * (xs + .5) / W - 1) - m20) / m00, ((1 - 2 * (ys + .5) / H) - m21) / m11, np.ones((H, W))], -1); rl = np.linalg.norm(ray, axis=-1)
dist = np.where(geo, np.where(geo, z, 0) * rl, np.inf); cosv = (ray / rl[..., None]) @ sun
LUMA = np.array([.2126, .7152, .0722]); luma = lin @ LUMA
lit_px = geo & (luma > np.percentile(luma[geo], 50)); E = 2 * np.pi * np.percentile(luma[lit_px], 90)  # the mock's estimator; the live pass uses the tracked light instead
phase = (1 - a.g ** 2) / (4 * np.pi * (1 + a.g ** 2 - 2 * a.g * cosv) ** 1.5); T = np.exp(-a.tau * (1 - np.exp(-dist / a.radius)))

def visibility(P, cascades):
    V = np.ones(P.shape[:-1]); todo = np.ones(P.shape[:-1], bool); P4 = np.concatenate([P, np.ones(P.shape[:-1] + (1,))], -1)
    for i in cascades:
        if not valid[i]: continue
        s = P4 @ rows[i].T; ins = todo & (np.maximum(np.abs(s[..., 0]), np.abs(s[..., 1])) <= margin) & (s[..., 2] >= 0) & (s[..., 2] <= 1)
        u = np.clip(np.floor(((s[..., 0] * .5 + .5) + .5 / size[i]) * size[i]), 0, size[i] - 1).astype(np.int64)
        v = np.clip(np.floor(((-s[..., 1] * .5 + .5) + .5 / size[i]) * size[i]), 0, size[i] - 1).astype(np.int64)
        V = np.where(ins, maps[i][v, u] >= s[..., 2] - bias[i], V); todo &= ~ins
    return V
def lit_fraction(h, w, cascades, phase_index):
    hs = (slice(0, 2 * h, 2), slice(0, 2 * w, 2)); rayh = ray[hs]; diru = rayh / np.linalg.norm(rayh, axis=-1)[..., None]; hy, hx = np.mgrid[0:h, 0:w]
    ign = np.modf(52.9829189 * np.modf(0.06711056 * (hx + 5.588238 * phase_index) + 0.00583715 * (hy + 5.588238 * phase_index))[0])[0]
    Tend = np.exp(-a.tau * (1 - np.exp(-dist[hs] / a.radius))); acc = np.zeros((h, w))
    for k in range(STEPS):
        q = (k + ign) / STEPS; tau = -np.log(1 - q * (1 - Tend)); t = -a.radius * np.log(np.maximum(1 - tau / a.tau, 1e-12))
        acc += visibility(diru * t[..., None], cascades)
    return acc / STEPS
def upsample(Fh):
    h, w = Fh.shape; out = np.zeros((H, W)); wsum = np.zeros((H, W)); zf = np.where(geo, z, 1e9); zh = zf[0:2 * h:2, 0:2 * w:2]
    fy = (ys - .5) / 2; fx = (xs - .5) / 2; y0 = np.floor(fy).astype(int); x0 = np.floor(fx).astype(int)
    for dy in (0, 1):
        for dx in (0, 1):
            yy = np.clip(y0 + dy, 0, h - 1); xx = np.clip(x0 + dx, 0, w - 1)
            wd = (1 - np.abs(fy - (y0 + dy))) * (1 - np.abs(fx - (x0 + dx))) * (np.exp(-np.abs(zh[yy, xx] - zf) / (0.05 * np.minimum(zf, zh[yy, xx]))) + 1e-4)
            out += wd * Fh[yy, xx]; wsum += wd
    return out / wsum
def hue(mean): return np.clip(mean / max(mean @ LUMA, 1e-9), 0, 4)

sky = ~geo
# mock law
Fm = upsample(sum(lit_fraction(H // 2, W // 2, range(5), p) for p in range(a.cycle)) / a.cycle); hue_m = hue(lin[sky].mean(0))
# shader law: apply slots 1-3 (the valid cascades in order, without the first), 8-bit F, sparse clamped sky taps
slots = [i for i in range(5) if size[i]][1:4]
Fs = upsample(sum(np.round(lit_fraction((H + 1) // 2, (W + 1) // 2, slots, p) * 255) / 255 for p in range(a.cycle)) / a.cycle)
ty = np.minimum(((np.arange(96) + .5) / 96 * H).astype(int), H - 1); tx = np.minimum(((np.arange(96) + .5) / 96 * W).astype(int), W - 1)
taps = np.minimum(lin[np.ix_(ty, tx)], 4.); mask = sky[np.ix_(ty, tx)]; hue_s = hue(taps[mask].mean(0))
print('frame %d %dx%d tau_max=%g g=%g R=%g E_sun(mock estimator)=%.4g sun(view)=%s cascades valid=%s shader slots=%s' % (F, W, H, a.tau, a.g, a.radius, E, sun.round(3), [int(v) for v in valid], slots))
print('mock law   : F geometry mean %.3f sky mean %.3f px F<0.9 %.1f%% F<0.5 %.1f%% | sky mean linear %s hue %s' % (Fm[geo].mean(), Fm[sky].mean(), 100 * (Fm < .9).mean(), 100 * (Fm < .5).mean(), lin[sky].mean(0).round(5), hue_m.round(3)))
print('shader law : F geometry mean %.3f sky mean %.3f px F<0.9 %.1f%% F<0.5 %.1f%% | sky taps %d of 9216 hue %s' % (Fs[geo].mean(), Fs[sky].mean(), 100 * (Fs < .9).mean(), 100 * (Fs < .5).mean(), int(mask.sum()), hue_s.round(3)))
dF = np.abs(Fs - Fm); ins_m = (hue_m * (E * phase * Fm * (1 - T))[..., None]) @ LUMA; ins_s = (hue_s * (E * phase * Fs * (1 - T))[..., None]) @ LUMA
rel = np.abs(ins_s - ins_m) / np.maximum(ins_m, 1e-9)
print('shader - mock: |dF| mean %.4f p99 %.4f max %.3f, px |dF| > 1/16: %.2f%% | hue max rel %.4f | in-scatter luma rel diff mean %.4f p50 %.4f p99 %.4f | in-scatter luma mean mock %.5f shader %.5f'
      % (dF.mean(), np.percentile(dF, 99), dF.max(), 100 * (dF > 1 / 16).mean(), np.abs(hue_s / hue_m - 1).max(), rel.mean(), np.percentile(rel, 50), np.percentile(rel, 99), ins_m.mean(), ins_s.mean()))
