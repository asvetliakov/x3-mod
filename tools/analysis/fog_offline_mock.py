#!/usr/bin/env python3
"""Stage 0 offline mock-up of the volumetric sun fog (docs/architecture/volumetric-fog.md) over one --taa-debug dump frame.

usage: fog_offline_mock.py <dump dir> <frame> <png prefix> [--tau 0.01,0.03,0.08] [--radius 10000] [--aerial-half auto|units|0]
                           [--steps 32] [--g 0.6] [--gain 1] [--colour taa|hdr] [--sun-rgb 1,0.95,0.85] [--cycle 8]
                           [--albedo-mix 0.5] [--cards 1,2,4] [--card-radius 2500] [--sheet contact_sheet.png]
  --cards renders variant b (enhance the game's own nebulafog cards; see the comment above that block); --sheet writes a labelled
  contact sheet of the panels.  When the frame has no sun_shadow_apply_params row (sun lane refused) the view -> sun rows are rebuilt
  from shadow_replay_map_basis and the scene camera logged with a sector-camera draw.

Inputs (all from the dump, nothing synthesised except the sun colour and the medium):
  colour   taa_1_<f>.rgba16f (resolved) or hdr_1_<f>.rgba16f: engine-space FP16, decoded ^2.2 to linear as agx.hlsl does
  depth    depth_1_<f>.rgba32f: .r device z/w (-1 = sentinel/sky), .b = clip w (view depth) when the sun lane is active
  camera   sun_shadow_apply_params m00 m11 m20 m21 m22 m32 (the jittered projection latch of that frame)
  shadows  the five real cascade maps shadow_map<i>_1_<f>.r32f with the log's view -> sun rows<i>, looked up exactly as
           sun_shadow_cascade_apply_ps.hlsl selects them (first containing cascade, one nearest tap, bias = bias_max<i>).
           This replaces the screen-space depth march the brief allowed as a stand-in: the dumps hold the real maps.
  sun dir  -normalize(rows0 row 2 xyz): the view-space direction toward the sun.  Sun COLOUR is not logged: --sun-rgb.
  sector   object_fade near36c x scale of the camera whose flags270 has 0x10000 (the engine's sector fog start N s, units)
  exposure hdr_frame ev_adapted; display = the AgX port taa_resolve_replay.py uses (agx_reference.py), no bloom, no RCAS.

Model (the note's section 3), two terms composited in linear light:
  A aerial perspective, geometry only: L <- L Ta + Sky_local (1 - Ta), Ta = 2^-(d / aerial half distance); half distance =
    the sector's N s (auto), Sky_local = the sky-only image blurred to ~1/16 resolution.  Sky pixels are untouched.
  B sun medium local to the camera: density sigma0 exp(-t / R), optical depth tau(d) = tau_max (1 - exp(-d / R)) analytic at
    full resolution (sky: tau_max).  The march runs at HALF resolution and returns only the scalar lit fraction
    F = int sigma T V dt / (1 - T) (V = cascade visibility; samples equal-weight in transmittance), jittered per pixel with
    interleaved gradient noise, averaged over --cycle phases (what the 8-sample TAA converges to) and joint-bilaterally
    upsampled by view depth.   L <- L Tb + albedo E_sun p_HG(cos) F gain (1 - Tb),
    E_sun = 2 pi x p90 linear luma of lit geometry (a 0.5-albedo Lambert surface), albedo = lerp(white, mean sky hue, 0.5).
Every --tau is rendered as given (forced); the automatic sector rule's tau is printed beside it.  Prints aggregates only."""
import argparse, glob, os, re, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import agx_reference as ar
from PIL import Image

ap = argparse.ArgumentParser(); ap.add_argument('dump'); ap.add_argument('frame', type=int); ap.add_argument('prefix')
ap.add_argument('--tau', default='0.01,0.03,0.08'); ap.add_argument('--radius', type=float, default=10000.); ap.add_argument('--aerial-half', default='auto'); ap.add_argument('--steps', type=int, default=32); ap.add_argument('--g', type=float, default=0.6)
ap.add_argument('--gain', type=float, default=1.); ap.add_argument('--colour', default='taa')
ap.add_argument('--sun-rgb', default='1,0.95,0.85'); ap.add_argument('--cycle', type=int, default=8)
ap.add_argument('--cards', default='', help='variant b boosts, e.g. 1,2,4: enhance the game\'s own nebulafog cards instead of adding a medium'); ap.add_argument('--card-radius', type=float, default=2500.); ap.add_argument('--albedo-mix', type=float, default=.5, help='0 white .. 1 the mean sky hue'); ap.add_argument('--sheet', default='')
a = ap.parse_args()
D = a.dump.rstrip('/') + '/'; F = a.frame; log = glob.glob(D + 'session-*.log')[0]
def grep1(kind): return subprocess.run(['grep', '-m1', '-E', r'^%s device=1 frame=%d ' % (kind, F), log], capture_output=True, text=True).stdout
line = grep1('sun_shadow_apply_params'); kv = dict(re.findall(r'(\w+)=([-\w.+,]+)', line))
def f32(h): return float(np.array([int(h, 16)], np.uint32).view(np.float32)[0])
def scene_camera():
    """Fallback when the sun lane refused the frame (no sun_shadow_apply_params row): the view and projection rows logged with the
    first draw scoped to the sector camera (object_fade flags270 & 0x10000).  Row-vector convention, p_view = p_world @ view."""
    txt = subprocess.run(['grep', '-A60', '-m400', '-E', r'^object_context device=1 frame=%d ' % F, log], capture_output=True, text=True).stdout
    cam = None; blocks = txt.split('object_context ')
    for b in blocks:
        m = re.search(r'object_fade .*camera=(\w+) .*flags270=(\w+)', b)
        if m and int(m.group(2), 16) & 0x10000: cam = m.group(1); break
    for b in blocks:
        if cam and re.match(r'.*? camera=%s ' % cam, b.split('\n')[0]):
            M = {r: [[f32(x) for x in bits.split(',')] for role, bits in re.findall(r'object_matrix role=(\w+) row=\d bits=(\S+)', b) if role == r] for r in ('view', 'projection')}
            if len(M['view']) == 4 and len(M['projection']) == 4: return np.array(M['view']), np.array(M['projection'])
    raise SystemExit('no sector-camera draw with matrices in frame %d' % F)
if 'width' in kv:
    W, H = int(kv['width']), int(kv['height']); m00, m11, m20, m21, m22, m32 = (float(kv[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32')); margin = float(kv['margin'])
    rows = [np.array(kv['rows%d' % i].split(','), np.float64).reshape(3, 4) for i in range(5)]
    valid = [kv['valid%d' % i] == '1' for i in range(5)]; size = [int(kv['map%d' % i]) for i in range(5)]; bias = [float(kv['bias_max%d' % i]) for i in range(5)]
else:  # lane refused: rebuild view -> sun rows from shadow_replay_map_basis (world space, shadow_replay_projection.h) and the scene camera
    print('no sun_shadow_apply_params row (sun lane refused this frame): cascade rows rebuilt from shadow_replay_map_basis + the logged scene camera; jitter ignored')
    W, H = (int(x) for x in re.search(r'target=(\d+)x(\d+)', grep1('hdr_frame')).groups()); Vm, Pm = scene_camera()
    m00, m11, m20, m21, m22, m32 = Pm[0, 0], Pm[1, 1], 0., 0., Pm[2, 2], Pm[3, 2]; margin = 1.; Rv, tv = Vm[:3, :3], Vm[3, :3]
    rows, valid, size, bias = [np.zeros((3, 4))] * 5, [False] * 5, [0] * 5, [0.] * 5
    for l in subprocess.run(['grep', '-E', r'^shadow_replay_map_basis device=1 frame=%d ' % F, log], capture_output=True, text=True).stdout.splitlines():
        b = dict(re.findall(r'(\w+)=([-\w.+,]+)', l)); i = int(b['cascade']); ax = [np.array(b[k].split(','), np.float64) for k in ('right', 'up', 'forward')]; c = np.array(b['center'].split(','), np.float64)
        E_, Ld, Bd = float(b['extent']), float(b['depth_light']), float(b['depth_behind']); sc = [1 / E_, 1 / E_, 1 / (Ld + Bd)]; off = [0., 0., Ld / (Ld + Bd)]
        # p_world = (p_view - tv) @ Rv.T ; s_k = ((p_world - c) . ax_k) sc_k + off_k
        rows[i] = np.array([np.concatenate([(Rv.T @ ax[k]) * sc[k], [(-(tv @ Rv.T) - c) @ ax[k] * sc[k] + off[k]]]) for k in range(3)])
        valid[i] = b['valid'] == '1'; size[i] = int(b['size']); bias[i] = 3. * (2 * E_ / size[i]) / (Ld + Bd)
ev = float(re.search(r'ev_adapted=([-\d.]+)', grep1('hdr_frame')).group(1))
maps = [np.fromfile(D + 'shadow_map%d_1_%d.r32f' % (i, F), np.float32).reshape(size[i], size[i]) if valid[i] else None for i in range(5)]
light = rows[0][2, :3] / np.linalg.norm(rows[0][2, :3]); sun = -light
ds = np.fromfile(D + 'depth_1_%d.rgba32f' % F, np.float32).reshape(H, W, 4).astype(np.float64)
geo = (ds[..., 0] >= 0) & (ds[..., 0] <= 1)
z = np.where(geo, np.where(ds[..., 2] > 0, ds[..., 2], m32 / (ds[..., 0] - m22)), np.inf)
src = a.colour if os.path.exists(D + '%s_1_%d.rgba16f' % (a.colour, F)) else 'hdr'
eng = np.nan_to_num(np.fromfile(D + '%s_1_%d.rgba16f' % (src, F), np.float16).reshape(H, W, 4)[..., :3].astype(np.float64), nan=0, posinf=65504, neginf=0)
lin = np.maximum(eng, 0) ** 2.2
ys, xs = np.mgrid[0:H, 0:W]
ray = np.stack([((2 * (xs + .5) / W - 1) - m20) / m00, ((1 - 2 * (ys + .5) / H) - m21) / m11, np.ones((H, W))], -1)  # view position per unit view z
rl = np.linalg.norm(ray, axis=-1); dist = np.where(geo, z * rl, np.inf)
cosv = (ray / rl[..., None]) @ sun
LUMA = np.array([.2126, .7152, .0722]); luma = lin @ LUMA
lit = geo & (luma > np.percentile(luma[geo], 50)) if geo.any() else geo
E = 2 * np.pi * (np.percentile(luma[lit], 90) if lit.any() else 0.05); sunrgb = np.array(a.sun_rgb.split(','), np.float64)
sky = ~geo; skymean = lin[sky].mean(0) if sky.any() else np.zeros(3); skyhue = skymean / max(skymean @ LUMA, 1e-9)
albedo = (1 - a.albedo_mix) + a.albedo_mix * np.clip(skyhue, 0, 4)
print('frame %d %dx%d colour=%s ev=%.3f sun(view, toward)=%s  geometry %.1f%%  distance p5/p50/p95=%s  E_sun=%.4g  sky mean linear=%s albedo=%s'
      % (F, W, H, src, ev, sun.round(3), 100 * geo.mean(), np.percentile((z * rl)[geo], [5, 50, 95]).round(0) if geo.any() else '-', E, skymean.round(5), albedo.round(2)))

def visibility(P):
    """P: (..., 3) view positions -> 1 lit / 0 shadowed; first containing valid cascade, lit outside all (beyond 150k units)."""
    V = np.ones(P.shape[:-1]); todo = np.ones(P.shape[:-1], bool); P4 = np.concatenate([P, np.ones(P.shape[:-1] + (1,))], -1)
    for i in range(5):
        if not valid[i]: continue
        s = P4 @ rows[i].T; ins = todo & (np.maximum(np.abs(s[..., 0]), np.abs(s[..., 1])) <= margin) & (s[..., 2] >= 0) & (s[..., 2] <= 1)
        u = np.clip(np.floor(((s[..., 0] * .5 + .5) + .5 / size[i]) * size[i]).astype(np.int64), 0, size[i] - 1)
        v = np.clip(np.floor(((-s[..., 1] * .5 + .5) + .5 / size[i]) * size[i]).astype(np.int64), 0, size[i] - 1)
        V = np.where(ins, maps[i][v, u] >= s[..., 2] - bias[i], V); todo &= ~ins
    return V

h, w = H // 2, W // 2; hs = (slice(0, 2 * h, 2), slice(0, 2 * w, 2))
rayh, disth, zh = ray[hs], dist[hs], np.where(geo, z, 1e9)[hs]; diru = rayh / np.linalg.norm(rayh, axis=-1)[..., None]
hy, hx = np.mgrid[0:h, 0:w]
def lit_fraction(tau_max, phase_index, radius=None):
    """F = sum sigma T V dt / (1 - T) over a.steps samples uniform in transmittance (importance sampling of sigma T), IGN-jittered."""
    ign = np.modf(52.9829189 * np.modf(0.06711056 * (hx + 5.588238 * phase_index) + 0.00583715 * (hy + 5.588238 * phase_index))[0])[0]
    radius = radius or a.radius; Tend = np.exp(-tau_max * (1 - np.exp(-disth / radius))); acc = np.zeros((h, w))
    for k in range(a.steps):
        q = (k + ign) / a.steps; tau = -np.log(1 - q * (1 - Tend))  # equal-weight samples of the sigma T density on [0, d]
        t = -radius * np.log(np.maximum(1 - tau / tau_max, 1e-12))
        acc += visibility(diru * t[..., None])
    return acc / a.steps
def upsample(Fh):
    """joint bilateral 2x2 by view depth (relative 5 %), the depth-aware upsample of the note."""
    out = np.zeros((H, W)); wsum = np.zeros((H, W)); zf = np.where(geo, z, 1e9)
    fy = (ys - .5) / 2; fx = (xs - .5) / 2; y0 = np.floor(fy).astype(int); x0 = np.floor(fx).astype(int)
    for dy in (0, 1):
        for dx in (0, 1):
            yy = np.clip(y0 + dy, 0, h - 1); xx = np.clip(x0 + dx, 0, w - 1)
            wb = (1 - np.abs(fy - (y0 + dy))) * (1 - np.abs(fx - (x0 + dx)))
            wd = wb * (np.exp(-np.abs(zh[yy, xx] - zf) / (0.05 * np.minimum(zf, zh[yy, xx]))) + 1e-4)
            out += wd * Fh[yy, xx]; wsum += wd
    return out / wsum
def agx(linear):
    v = np.maximum(linear, 1e-10) * 2. ** ev
    v = np.clip(np.log2(np.maximum(v @ np.array(ar.M_IN).T, ar.LOG_FLOOR)), ar.MIN_EV, ar.MAX_EV)
    v = (v - ar.MIN_EV) / (ar.MAX_EV - ar.MIN_EV); v = sum(ck * v ** (6 - i) for i, ck in enumerate(ar.CONTRAST_COEFFICIENTS))
    return np.clip(v @ np.array(ar.M_OUT).T, 0, 1)
panels = {}
def save(name, linear):
    panels[name] = Image.fromarray((agx(linear) * 255 + .5).astype(np.uint8)); panels[name].save('%s_%s.png' % (a.prefix, name)); print('  wrote %s_%s.png' % (a.prefix, name))

g = a.g; phase = (1 - g * g) / (4 * np.pi * (1 + g * g - 2 * g * cosv) ** 1.5)
# sector rule inputs: the engine's fog start N s from the sector camera (flags270 & 0x10000), in shader-world units
Ns = None
for l in subprocess.run(['grep', '-m8', '-E', r'^object_fade device=1 frame=%d ' % F, log], capture_output=True, text=True).stdout.splitlines():
    f = dict(re.findall(r'(\w+)=([0-9a-f]+)', l))
    if int(f['flags270'], 16) & 0x10000: Ns = int(f['near36c'], 16) * float(np.array([int(f['scale_bits'], 16)], np.uint32).view(np.float32)[0])
ahalf = (Ns or 0) if a.aerial_half == 'auto' else float(a.aerial_half)
auto_tau = 0.03 * min(2., 250000. / Ns) if Ns else 0.  # the note's rule: tau_ref 0.03 at N s = 250k units
print('sector fog start N s = %s units -> automatic rule at strength 1: aerial half distance %s, tau_max %.4f' % (Ns, ahalf or 'off', auto_tau))
# term A: local sky colour = sky-only image, masked box mean at 1/16 resolution, bilinear back up
def small(img): return np.asarray(Image.fromarray(img.astype(np.float32)).resize((W // 16, H // 16), Image.BOX), np.float64)
def big(img): return np.asarray(Image.fromarray(img.astype(np.float32)).resize((W, H), Image.BILINEAR), np.float64)
cover = small(sky.astype(np.float64)); skyloc = np.stack([big(small(lin[..., c] * sky) / np.maximum(cover, 1e-3)) for c in range(3)], -1)
skyloc = np.where(big(cover)[..., None] > 0.02, skyloc, skymean)
Ta = np.where(geo, 2. ** (-dist / ahalf), 1.) if ahalf else np.ones((H, W))
base = lin * Ta[..., None] + skyloc * (1 - Ta)[..., None]
save('baseline', lin)
if ahalf: save('aerial_only', base); print('  aerial transmittance over geometry p5/p50/p95=%s' % np.percentile(Ta[geo], [5, 50, 95]).round(3))
for tm in [float(x) for x in a.tau.split(',')]:
    T = np.exp(-tm * (1 - np.exp(-dist / a.radius)))
    one = lit_fraction(tm, 0); Fh = (sum(lit_fraction(tm, p) for p in range(1, a.cycle)) + one) / a.cycle
    Fu = upsample(Fh); F1 = upsample(one)
    for name, Fr in (('shafts', Fu), ('noshafts', np.ones((H, W))), ('shafts_1frame', F1)):
        ins = albedo * sunrgb * (E * a.gain * phase * Fr * (1 - T))[..., None]
        save('tau%03d_%s' % (round(tm * 100), name), base * T[..., None] + ins)
    noise = np.abs(F1 - Fu); wash = (albedo * sunrgb * (E * a.gain * phase * (1 - T))[..., None]) @ LUMA
    print('tau_max=%g R=%g: T geometry p5/p50/p95=%s T sky=%.3f | unshadowed sun wash linear luma sky p50/p99=%s (sky mean luma %.4g) | lit fraction F: geometry mean %.3f sky mean %.3f, px F<0.9: %.1f%%, F<0.5: %.1f%% | one-frame |F - cycle mean| mean %.4f p99 %.3f'
          % (tm, a.radius, np.percentile(T[geo], [5, 50, 95]).round(3) if geo.any() else '-', np.exp(-tm), np.percentile(wash[sky], [50, 99]).round(4) if sky.any() else '-', skymean @ LUMA,
             Fu[geo].mean() if geo.any() else 1, Fu[sky].mean() if sky.any() else 1, 100 * (Fu < .9).mean(), 100 * (Fu < .5).mean(), noise.mean(), np.percentile(noise, 99)))

# Variant b: enhance the game's own nebulafog cards (PS f7e0b6647a3bfa62: ZENABLE 0, ONE/INVSRCCOLOR screen blend in engine space, drawn
# last before the scene end, never depth tested) instead of adding a medium.  No with/without pair exists and the card texture is not in the
# dump, so only the UNIFORM part of the card layer is recoverable: the black floor the screen blend leaves on sky and on dark geometry alike.
# Its cloud structure cannot be separated from the painted background in one frame.  b = invert the screen blend with that floor, then
# re-apply the layer x boost x soft-particle fade against scene depth x sun colour x clipped HG phase x marched lit fraction over the card shell.
if a.cards:
    e = np.maximum(eng, 0); sk = sky.copy()
    for _ in range(3): sk &= np.roll(sk, 1, 0) & np.roll(sk, -1, 0) & np.roll(sk, 1, 1) & np.roll(sk, -1, 1)
    cf = np.minimum(np.percentile(e[sk], 5, axis=0) if sk.any() else 1., np.percentile(e[geo], 10, axis=0) if geo.any() else 1.)
    dst = np.clip((e - cf) / (1 - cf), 0, None); save('cards_removed', dst ** 2.2)
    Fb = upsample(sum(lit_fraction(0.05, p, a.card_radius) for p in range(a.cycle)) / a.cycle)
    soft = np.where(geo, np.clip(dist / (0.6 * a.card_radius), 0, 1) ** 2 * (3 - 2 * np.clip(dist / (0.6 * a.card_radius), 0, 1)), 1.)
    rel = np.clip(phase * 4 * np.pi, 0.5, 3.)
    print('variant b: card floor (engine space) %s = %.0f%% of the sky median %s; lit fraction over the %g-unit card shell: mean %.3f, px < 0.9: %.1f%%; soft fade < 0.5 on %.1f%% of geometry'
          % (cf.round(4), 100 * (cf @ LUMA) / max(np.median(e[sk], axis=0) @ LUMA, 1e-9) if sk.any() else 0, np.median(e[sk], axis=0).round(4) if sk.any() else '-', a.card_radius, Fb.mean(), 100 * (Fb < .9).mean(), 100 * (soft[geo] < .5).mean() if geo.any() else 0))
    for k in [float(x) for x in a.cards.split(',')]:
        for name, Fr in (('lit', Fb), ('unlit', np.ones((H, W)))):
            layer = np.clip(cf * k * sunrgb / sunrgb.max() * (soft * (rel * Fr if name == 'lit' else 1.))[..., None], 0, 0.95)
            save('cards_x%g_%s' % (k, name), (layer + dst * (1 - layer)) ** 2.2)
if a.sheet:
    from PIL import ImageDraw
    taus = ['tau%03d_shafts' % round(float(x) * 100) for x in a.tau.split(',')][:3]; cards = ['cards_x%g_lit' % float(x) for x in a.cards.split(',')][:3] if a.cards else []
    order = [('baseline', 'baseline (game fog cards as drawn)'), ('cards_removed', 'game fog cards removed (uniform-floor estimate)'), ('aerial_only', 'a: sector haze only')] + [(t, 'a: haze + sun medium %s, shafts' % t[:6]) for t in taus] + [(c, 'b: game cards soft-faded + sun-lit, %s' % c[6:].replace('_lit', '')) for c in cards]
    order = [(n, t) for n, t in order if n in panels]; cw, ch = W // 2, H // 2; sheet = Image.new('RGB', (cw * 3, ch * ((len(order) + 2) // 3)))
    for i, (n, t) in enumerate(order):
        im = panels[n].resize((cw, ch), Image.LANCZOS); dr = ImageDraw.Draw(im); dr.rectangle((0, 0, cw, 13), fill=(0, 0, 0)); dr.text((4, 1), t, fill=(255, 255, 0)); sheet.paste(im, ((i % 3) * cw, (i // 3) * ch))
    sheet.save(a.sheet); print('  wrote %s' % a.sheet)
