#!/usr/bin/env python3
"""Per-ray reference export for the stored-density shader fixture (checkpoint 2).

Imports tools/analysis/fog_density_runtime_screen.py unchanged (its digest is pinned by the
screen's tracked summary) and writes, for the screen's two poses: the filtered 24+40
candidate and filtered dense64 (S,T) of the stratified + central-crop sky rays, the 5
witness rays x 7 geometry depths, and the 5 witness rays at the seven forward shifts.
`cases.txt` routes the fixture; `reference.npz` is read by the checker only.

The single look (src/renderer/fog_look_math.h, FOG_LOOK in src/fog/fog_density_field_inc.h; the presets
L0/L1/L3 were retired on 2026-09-22): `look_constants` mirrors fog_look_constants and `look_march` the
shaped law on the same 24+40 bins; look cases carry `look phase=K shadow=0|1|2` after the mode and value
(1: a dark map, every sample shadowed; 2: the striped occluder of `stripe_visibility`, which exercises the
offset shaft lookup), then optionally `resolved=0` (no temporal resolve: the lookup offset is dropped).

`--march-scale 4` (docs/architecture/fog-gpu-cost.md, step C): the variant reference of the quarter-resolution march of the
same 256x144 screen. The look cases (option `scale=4`) are marched on every pixel of the 64x36 quarter grid (ray and shaft
lookup cell = the quarter pixel), and the look repair split's repaired rays go through (x+2, y+2)/full (the quarter
target's c0); the law is unchanged, so only the ray set and the lookup cells differ from the default reference.

The look marches 40 far bins; the 24-bin variant (`--far-bins 24`) and the visibility-grid twin went with --fog-far-bins
and --fog-shadow-pass on 2026-09-25.
"""
from __future__ import annotations
import argparse, importlib.util, json, math
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('fog_density_runtime_screen', HERE / 'fog_density_runtime_screen.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
SHIFTS = (-5500., -5000., -4500., 0., 4500., 5000., 5500.)
COVER_WAVES = ((1., -2., 1.), (2., 1., -1.), (-1., 1., 2.))  # coverage variation wave vectors, cycles per 65536 units
# Defaults of renderer::FogLookTuning.
TUNING = dict(coverage=.35, exponent=2., sigma_scale=8., coverage_variation=.12, warp_cycles_near=13., warp_near=500.,
              warp_cycles_far=5., warp_far=1400., forward_g=.75, forward_weight=.7, back_g=-.15, albedo_white=.5,
              ambient_gain=.35, extinction_tint=.6, scatter_lift=.5, lift_floor=.5, shadow_floor=.15, sky_cap=112500., taper_start=65000.,
              self_shadow=3., powder=.5, tap_distance=3000., tap_length=9000., shadow_jitter=1.)


def look_constants(chroma, radiance_over_pi=(1., 1., 1.), phase=0, tuning=None, resolved=True):
    """fog_look_constants: the eleven rows c25..c35 (float32 like the header) and the sigma factor."""
    t = {k: np.float32(v) for k, v in (tuning or TUNING).items()}; f = np.float32
    rows = np.zeros((11, 4), np.float32)
    mean = (f(radiance_over_pi[0]) + f(radiance_over_pi[1]) + f(radiance_over_pi[2])) / f(3)
    rows[0] = (t['coverage'], f(1) / (f(1) - t['coverage']), t['exponent'], t['sky_cap'])
    c = np.clip(np.asarray(chroma, np.float32), 0, 1); rotated = c[[2, 0, 1]]
    rows[1, :3] = c + (f(1) - c) * t['albedo_white']
    rows[3, :3] = t['ambient_gain'] * mean * c
    rows[2, :3] = t['ambient_gain'] * mean * (f(.3) * (c + rotated))
    rows[8, :3] = f(1) + t['extinction_tint'] * (f(1) - c)
    rows[1, 3] = t['shadow_floor']; rows[2, 3] = f(.25) * t['scatter_lift']; rows[3, 3] = t['lift_floor']
    for i, (g, w) in enumerate(((t['forward_g'], t['forward_weight']), (t['back_g'], f(1) - t['forward_weight']))):
        rows[4 + i, :3] = (f(1) + g * g, f(2) * g, f(.25) * w * (f(1) - g * g))
    rows[6, :2] = (t['self_shadow'], t['powder'])  # rows[6, 2:], the retired L3 sample offset, stay zero
    rows[7, :2] = (t['tap_distance'], t['tap_length']); rows[7, 2:] = t['shadow_jitter'] if resolved else f(0)
    rows[9] = (f(int(t['warp_cycles_far'] + f(.5))) / f(65536), t['warp_far'], f(int(t['warp_cycles_near'] + f(.5))) / f(65536), t['warp_near'])
    start = t['taper_start'] if t['taper_start'] <= t['sky_cap'] - f(1000) else f(.75) * t['sky_cap']
    rows[10, :3] = (t['coverage_variation'] / f(3), start, f(1) / (t['sky_cap'] - start))
    rows[8, 3] = f(5.588238) * f(phase % 64)
    return rows, float(t['sigma_scale'])


def look_noise(px, py, shift):
    """The shader's interleaved gradient noise in float32, and the distance of both frac() arguments
    from a wrap (the checker leaves out pixels where float rounding could land on the other side)."""
    f = np.float32
    x = np.asarray(px, np.float32) + f(shift); y = np.asarray(py, np.float32) + f(shift)
    inner = x * f(0.06711056) + y * f(0.00583715); a = inner - np.floor(inner)
    outer = f(52.9829189) * a; n = outer - np.floor(outer)
    margin = np.minimum(np.minimum(a, 1 - a) * 52.9829189, np.minimum(n, 1 - n))
    return n.astype(np.float64), margin.astype(np.float64)


def look_march(origin, directions, limits, solid, chroma, store, sigma, phase=0, pixels=None, shadowed=False, sun=(1., 0., 0.), visibility=None, tuning=None, resolved=True):
    """(S, T) of the FOG_LOOK law. `limits` is the geometry distance (ignored for sky rays; both end at the cap), `pixels` the
    half-resolution (x, y) of each ray for the shaft lookup offset while `resolved`
    (a repair pixel passes None: the repair law keeps the bin centres), `shadowed` a shaft visibility of 0 on every sample, `visibility(points, rays, ds)` the
    shaft visibility at camera-relative world `points` of ray indices `rays` in bins of length `ds` (the shaft lookup
    position, which is not the density position when only the lookup is offset). 40 far bins (FOG_FAR_BINS)."""
    far_bins = 40
    k, scale = look_constants(chroma, phase=phase, tuning=tuning, resolved=resolved); k = k.astype(np.float64); sigma = float(np.float32(sigma * scale))
    d = np.asarray(directions, np.float64); n = len(d); sun = np.asarray(sun, np.float64); origin = np.asarray(origin, np.float64)
    solid = np.broadcast_to(np.asarray(solid, bool), (n,))
    # Every ray ends at the column cap, sky and geometry alike (no silhouette rim around distant hulls).
    L = np.minimum(np.where(solid, np.asarray(limits, np.float64), m.FAR), min(m.FAR, k[0, 3]))
    taper_start = np.full(n, k[10, 1]); taper_recip = np.full(n, k[10, 2])
    near = np.minimum(L, 12000.) / 24; far = np.maximum(L - 12000., 0) / far_bins
    offset = np.full((n, 4), .5)  # bins: near and far of the density sample, near and far of the shaft lookup
    if pixels is not None:
        noise, _ = look_noise(pixels[0], pixels[1], k[8, 3]); offset += (noise - .5)[:, None] * np.concatenate((k[6, 2:], k[7, 2:]))[None, :]

    def density(rho, cover):
        x = np.clip((rho - k[0, 0] - cover) * k[0, 1], 0, 1); return np.where(x > 0, x ** k[0, 2], 0.)

    def wave(x):
        t = x - np.floor(x) - .5; return t * (8 - 16 * np.abs(t))
    # c22.xyz of the fixture: the camera modulo the fine window (65536 units), centred, in float32.
    local = (origin - 65536. * np.floor(origin / 65536. + .5)).astype(np.float32).astype(np.float64)
    lit = np.zeros(n); lift = np.zeros(n); T = np.ones(n)
    rays = np.arange(n)
    for i in range(24 + far_bins):
        ds = near if i < 24 else far
        s = near * (i + offset[:, 0]) if i < 24 else 12000. + far * (i - 24 + offset[:, 1])
        active = ds > 0
        if not active.any():
            continue
        ray = d[active] * s[active, None]; world = local + ray
        wave1 = wave(world[:, [1, 2, 0]] * k[9, 0]); wave2 = wave(world[:, [2, 0, 1]] * k[9, 2])
        offsets = wave1 * k[9, 1] + wave2 * k[9, 3]; points = origin + ray + offsets; warped = (world + offsets) / 65536.
        # Three oblique plane waves, whole cycles per fine window, at the warped position (no axis-aligned slabs).
        cover = k[10, 0] * sum(wave(warped[:, 0] * a + warped[:, 1] * b + warped[:, 2] * c) for a, b, c in COVER_WAVES)
        rho = density(store.sample(points, s[active], origin).astype(np.float64), cover)
        e = np.clip((s[active] - taper_start[active]) * taper_recip[active], 0, 1); rho = rho * (1 - e * e * (3 - 2 * e))
        step = sigma * rho * ds[active]; a = 1 - np.exp(-step); a2 = 1 - np.exp(-.5 * step)
        sunward = np.ones(len(rho)); fogged = rho > 0
        if fogged.any():
            r = density(store.sample_level('far', points[fogged] + sun * k[7, 0], origin).astype(np.float64), cover[fogged]) * k[7, 1]
            tau = sigma * k[6, 0] * r
            sunward[fogged] = np.exp(-tau) * (1 - k[6, 1] * np.exp(-2 * (tau + sigma * k[6, 0] * rho[fogged] * k[7, 0])))
        vis = np.ones(len(rho))
        if shadowed:
            vis[fogged] = 0.
        elif visibility is not None and fogged.any():
            shaft = (near * (i + offset[:, 2]) if i < 24 else 12000. + far * (i - 24 + offset[:, 3]))[active][fogged]
            vis[fogged] = visibility(d[active][fogged] * shaft[:, None], rays[active][fogged], ds[active][fogged])
        lit[active] += T[active] * a * sunward * (k[1, 3] + (1 - k[1, 3]) * vis)
        lift[active] += T[active] * a2 * (k[3, 3] + (1 - k[3, 3]) * vis)
        T[active] *= 1 - a
    cosine = d[:, 0] * sun[0] + d[:, 1] * sun[1] + d[:, 2] * sun[2]
    lobes = [k[4 + j, 2] / (k[4 + j, 0] - k[4 + j, 1] * cosine) ** 1.5 for j in range(2)]
    ambient = k[2, :3][None, :] + (k[3, :3] - k[2, :3])[None, :] * (.5 + .5 * cosine)[:, None]
    S = k[1, :3][None, :] * (((lobes[0] + lobes[1]) * lit + k[2, 3] * lift)[:, None] + ambient * (1 - T)[:, None])  # E/pi = 1
    return S.astype(m.F), T.astype(m.F)


STRIPE_SPAN = 120000.  # view depth across the striped map of the shadow=2 fixture case


def stripe_map(size=64):
    """64 columns: 0 (occluder at the light) in every other pair (3750 units of view depth, 1.5 far bins) between columns 8 and 55, 1 elsewhere, so
    the map is lit on both sides of the blend-band start (|x| = .85) and visibility is continuous along a ray."""
    c = np.arange(size); edge = size // 8; return np.where(((c // 2) % 2 == 1) & (c >= edge) & (c < size - edge), 0., 1.)


def stripe_visibility(forward, span=STRIPE_SPAN, size=64):
    """fog_look_visibility of the shadow=2 case: cascade 0 maps view depth z to x = 2 z / STRIPE_SPAN - 1, y = 0, depth .5
    (bias .001) over a 64-texel map whose rows all hold stripe_map(); the 2x2 comparison is linear in x."""
    forward = np.asarray(forward, np.float64); columns = stripe_map(size) >= .5 - .001

    def visibility(points, rays, ds):
        x = 2. * (points @ forward) / span - 1.; texel = (x * .5 + .5) * size
        base = np.floor(texel); f = texel - base; i = np.clip(base.astype(int), 0, size - 1); j = np.clip(i + 1, 0, size - 1)
        return np.where(np.abs(x) <= .85, columns[i] * (1 - f) + columns[j] * f, 1.)
    return visibility


def basis(pose):
    forward = np.asarray(pose['forward'], np.float64); up = np.asarray(pose['up'], np.float64)
    right = np.cross(up, forward); right /= np.linalg.norm(right); up = np.cross(forward, right)
    return right, up, forward


def both(origin, directions, limits, chroma, store):
    ev = m.filtered_eval(store)
    cand = m.integrate(origin, directions, limits, chroma, ev, 'candidate', 0)['full']
    dense = m.integrate(origin, directions, limits, chroma, ev, 'dense', 64.)['full']
    return {'candidate_S': cand['S'], 'candidate_T': cand['T'], 'dense64_S': dense['S'], 'dense64_T': dense['T']}


def quarter_rays(pose, scale):
    """m.rays of the march grid at spacing `scale` over the same m.W x m.H half-resolution screen (scale 2: m.rays)."""
    w, h = 2 * m.W // scale, 2 * m.H // scale
    x, y = np.meshgrid(np.arange(w), np.arange(h)); u = (x + .5) / w; v = (y + .5) / h; t = math.tan(math.radians(30))
    local = np.stack(((2 * u - 1) * (w / h) * t, (1 - 2 * v) * t, np.ones_like(u)), axis=-1); local /= np.linalg.norm(local, axis=-1, keepdims=True)
    r, up, f = basis(pose); world = local[..., :1] * r + local[..., 1:2] * up + local[..., 2:] * f
    return (world / np.linalg.norm(world, axis=-1, keepdims=True)).reshape(-1, 3).astype(m.F), x.ravel(), y.ravel(), w


def run(asset_data, output, march_scale=2):
    if output.exists():
        raise ValueError(f'output directory already exists: {output}')
    output.mkdir(parents=True)
    manifest = json.loads((asset_data / 'manifest.json').read_text())
    profile = next(x for x in manifest['profiles'] if x['name'] == 'foggreenoutlands')
    if abs(float(profile['base_sigma']) * 1.5 - m.SIGMA) > 1e-15:
        raise ValueError('sigma')
    volume = m.fog.decode_packet(asset_data / 'foggreenoutlands.fogbin', manifest)
    chroma = (volume[..., :3].sum((0, 1, 2), dtype=np.float64) / volume[..., 3].sum(dtype=np.float64)).astype(m.F)
    store = m.LazyStore(); arrays = {}; pops = m.populations(); variant = march_scale != 2
    march = look_march
    scale_option = ' scale=%d' % march_scale if variant else ''
    lines = ['sigma %r' % float(m.SIGMA), 'chroma %r %r %r' % tuple(float(np.float32(c)) for c in chroma)]

    def case(name, origin, pose, mode, value, extra=''):
        r, u, f = basis(pose)
        lines.append(' '.join([name] + [repr(float(v)) for v in (*origin, *r, *u, *f)] + [mode, value] + extra.split()))

    for pose in m.screen.pose_rows():
        name = pose['name']; origin = np.asarray(pose['origin'], np.float64); allr = m.rays(pose)
        combined = np.unique(np.concatenate([y * m.W + x for x, y in pops.values()]))
        sx, sy = pops['stratified']; wi = np.array([j * 32 + i for i, j in m.WITNESS]); wpix = sy[wi] * m.W + sx[wi]
        t = math.tan(math.radians(30))
        if not variant:  # the unshaped parity cases: the default reference only
            case(f'{name}_sky', origin, pose, 'sky', '0')
            arrays[f'{name}_sky_pixels'] = combined
            for k, v in both(origin, allr[combined], np.full(len(combined), m.FAR, m.F), chroma, store).items():
                arrays[f'{name}_sky_{k}'] = v
            vl = np.linalg.norm(np.stack(((2 * (sx[wi] + .5) / m.W - 1) * (m.W / m.H) * t, (1 - 2 * (sy[wi] + .5) / m.H) * t, np.ones(5)), 1), axis=1)
            arrays[f'{name}_witness_pixels'] = wpix
            for index, depth in enumerate(m.DEPTHS):
                limits = np.minimum((depth / vl) * vl, m.FAR).astype(m.F)
                case(f'{name}_depth{index}', origin, pose, 'depth', repr(float(depth)))
                for k, v in both(origin, allr[wpix], limits, chroma, store).items():
                    arrays[f'{name}_depth{index}_{k}'] = v
            for index, shift in enumerate(SHIFTS):
                if shift == 0.:
                    continue
                moved = origin + shift * np.asarray(pose['forward'], np.float64)
                case(f'{name}_shift{index}', moved, pose, 'sky', '0')
                for k, v in both(moved, allr[wpix], np.full(5, m.FAR, m.F), chroma, store).items():
                    arrays[f'{name}_shift{index}_{k}'] = v
        if name == 'A':
            # The fixture's composite/repair split with the 1024-texel striped occluder (24000-unit span: 47-unit pairs against
            # the 200-unit far bins of these columns; phase 5): odd full-resolution
            # columns are geometry at view depth 20000 and are repaired. A stratified 32x18 subset; the fixture's repair ray
            # goes through (x+1, y+1)/full (its c0 is the half-resolution one). The repair program keeps the bin
            # centres (FOG_LOOK_NO_OFFSET); `_other` is the offset lookup, which the GPU result must not match.
            fx, fy = np.meshgrid(8 * np.arange(32) + 1, 8 * np.arange(18) + 4); fx = fx.ravel(); fy = fy.ravel(); t = math.tan(math.radians(30))
            c0 = march_scale // 2  # the repair ray's offset in full pixels under the march target's c0 (scale 2: +1, scale 4: +2)
            local = np.stack(((2 * (fx + c0) / (2 * m.W) - 1) * (m.W / m.H) * t, (1 - 2 * (fy + c0) / (2 * m.H)) * t, np.ones(len(fx))), 1)
            r, u, f = basis(pose); length = np.linalg.norm(local, axis=1); d = (local[:, :1] * r + local[:, 1:2] * u + local[:, 2:] * f) / length[:, None]
            arrays['A_repair_pixels'] = fy * 2 * m.W + fx; stripes = stripe_visibility(pose['forward'], 24000., 1024)
            # `_other` keeps the half-resolution lookup cells at every spacing: a control law measurably away from the bin
            # centres (the quarter cells happen to move these rays by less than twice the gate).
            for key, pixels in (('', None), ('_other', (fx // 2, fy // 2))):
                S, T = march(origin, d, 20000. * length, True, chroma, store, m.SIGMA, 5, pixels, visibility=stripes)
                arrays[f'A_repair_shafts{key}_S'] = S; arrays[f'A_repair_shafts{key}_T'] = T
            arrays['A_repair_extinction'] = look_constants(chroma)[0][8, :3]
        for index, kind in enumerate(() if variant else ('zero', 'nan', 'inf')):
            case(f'{name}_invalid{index}', origin, pose, 'invalid', kind)
        if not variant:
            case(f'{name}_empty', origin, pose, 'empty', '0')
        # The look on the stratified rays (half-resolution pixel = ray index): sky, two geometry depths across
        # the taper, the striped occluder with and without a temporal resolve, and the fully shadowed sky (coloured).
        strat = sy * m.W + sx; arrays[f'{name}_look_pixels'] = strat
        look_rays, look_xy = allr[strat], (sx, sy)
        if march_scale != 2:  # every pixel of the quarter grid, its own rays and lookup cells
            look_rays, qx, qy, qw = quarter_rays(pose, march_scale); look_xy = (qx, qy); arrays[f'{name}_look_pixels'] = qy * qw + qx
        looks = [(f'{name}_look_sky', 'sky', None, False)]
        if name == 'A':
            # shadow=2: the striped occluder, whose lookup is offset per pixel and frame (phase 5); `_held` is TAA
            # off, which drops the offset and holds the lookup at the bin centres.
            looks += [('A_look_stripes', 'sky', None, 2), ('A_look_stripes_held', 'sky', None, 2)]
            looks += [('A_look_depth3', 'depth', float(m.DEPTHS[3]), False), ('A_look_depth90000', 'depth', 90000., False), ('A_look_shadowed', 'sky', None, True)]
        for label, mode, depth, shadowed in looks:
            phase = 5 if shadowed == 2 else 0
            held = label.endswith('_held')
            case(label, origin, pose, mode, repr(depth) if depth else '0', f'look phase={phase} shadow={int(shadowed)}' + (' resolved=0' if held else '') + scale_option)
            n = len(look_rays)
            S, T = march(origin, look_rays, np.full(n, depth or m.FAR), depth is not None, chroma, store, m.SIGMA, phase, look_xy, shadowed is True,
                              visibility=stripe_visibility(pose['forward']) if shadowed == 2 else None, resolved=not held)
            arrays[f'{label}_S'] = S; arrays[f'{label}_T'] = T
            if shadowed == 2:  # what the offset lookup moves: the same case marched with the lookup at bin centres
                arrays[f'{label}_centre_delta'] = np.abs(S - march(origin, look_rays, np.full(n, m.FAR), False, chroma, store, m.SIGMA, phase, look_xy,
                                                                         visibility=stripe_visibility(pose['forward']), tuning=dict(TUNING, shadow_jitter=0.))[0]).max(1)
                arrays[f'{label}_noise_margin'] = look_noise(look_xy[0], look_xy[1], look_constants(chroma, phase=phase)[0][8, 3])[1] if not held else np.ones(n)
    (output / 'cases.txt').write_text('\n'.join(lines) + '\n')
    np.savez(output / 'reference.npz', **arrays)
    record = dict(schema=1, width=m.W, height=m.H, shifts=SHIFTS, depths=m.DEPTHS.tolist(),
                  screen_sha256=m.digest(HERE / 'fog_density_runtime_screen.py'), exporter_sha256=m.digest(__file__),
                  cases_sha256=m.digest(output / 'cases.txt'), reference_sha256=m.digest(output / 'reference.npz'),
                  manifest_sha256=m.digest(asset_data / 'manifest.json'), packet_sha256=m.digest(asset_data / 'foggreenoutlands.fogbin'))
    if march_scale != 2:
        record['march_scale'] = march_scale
    (output / 'reference.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--asset-data', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    p.add_argument('--march-scale', type=int, choices=(2, 4), default=2, help='march spacing: 2 the default reference, 4 the step C quarter-resolution variant')
    a = p.parse_args(); print(json.dumps(run(a.asset_data, a.output, a.march_scale)))


if __name__ == '__main__':
    main()
