#!/usr/bin/env python3
"""Per-ray reference export for the stored-density shader fixture (checkpoint 2).

Imports tools/analysis/fog_density_runtime_screen.py unchanged (its digest is pinned by the
screen's tracked summary) and writes, for the screen's two poses: the filtered 24+40
candidate and filtered dense64 (S,T) of the stratified + central-crop sky rays, the 5
witness rays x 7 geometry depths, and the 5 witness rays at the seven forward shifts.
`cases.txt` routes the fixture; `reference.npz` is read by the checker only.

Look presets L1-L3 (src/renderer/fog_look_math.h, FOG_LOOK in src/fog/fog_density_field_inc.h):
`look_constants` mirrors fog_look_constants and `look_march` the shaped law on the same 24+40
bins; look cases carry `look=N phase=K shadow=0|1` after the mode and value.
"""
from __future__ import annotations
import argparse, importlib.util, json, math
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('fog_density_runtime_screen', HERE / 'fog_density_runtime_screen.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
SHIFTS = (-5500., -5000., -4500., 0., 4500., 5000., 5500.)
LOOK_PHASE = 3  # TAA sequence index of the L3 cases
COVER_WAVES = ((1., -2., 1.), (2., 1., -1.), (-1., 1., 2.))  # coverage variation wave vectors, cycles per 65536 units
# Defaults of renderer::FogLookTuning.
TUNING = dict(coverage=.35, exponent=2., sigma_scale=8., coverage_variation=.12, warp_cycles_near=13., warp_near=500.,
              warp_cycles_far=5., warp_far=1400., forward_g=.75, forward_weight=.7, back_g=-.15, albedo_white=.5,
              ambient_gain=.35, extinction_tint=.6, scatter_lift=.5, lift_floor=.5, shadow_floor=.15, sky_cap=112500., taper_start=65000.,
              self_shadow=3., powder=.5, tap_distance=3000., tap_length=9000., jitter_near=1., jitter_far=1.)


def look_constants(look, chroma, radiance_over_pi=(1., 1., 1.), phase=0, tuning=None):
    """fog_look_constants: the eleven rows c25..c35 (float32 like the header) and the sigma factor."""
    t = {k: np.float32(v) for k, v in (tuning or TUNING).items()}; f = np.float32
    rows = np.zeros((11, 4), np.float32)
    if look == 0:
        return rows, 1.
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
    if look >= 2:
        rows[6, :2] = (t['self_shadow'], t['powder'])
    if look >= 3:
        rows[6, 2:] = (t['jitter_near'], t['jitter_far'])
    rows[7, :2] = (t['tap_distance'], t['tap_length'])
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


def look_march(origin, directions, limits, solid, chroma, store, look, sigma, phase=0, pixels=None, shadowed=False, sun=(1., 0., 0.)):
    """(S, T) of the FOG_LOOK law. `limits` is the geometry distance (ignored for sky rays; both end at the cap), `pixels` the
    half-resolution (x, y) of each ray for the L3 offset, `shadowed` a shaft visibility of 0 on every sample."""
    k, scale = look_constants(look, chroma, phase=phase); k = k.astype(np.float64); sigma = float(np.float32(sigma * scale))
    d = np.asarray(directions, np.float64); n = len(d); sun = np.asarray(sun, np.float64); origin = np.asarray(origin, np.float64)
    solid = np.broadcast_to(np.asarray(solid, bool), (n,))
    # Every ray ends at the column cap, sky and geometry alike (no silhouette rim around distant hulls).
    L = np.minimum(np.where(solid, np.asarray(limits, np.float64), m.FAR), min(m.FAR, k[0, 3]))
    taper_start = np.full(n, k[10, 1]); taper_recip = np.full(n, k[10, 2])
    near = np.minimum(L, 12000.) / 24; far = np.maximum(L - 12000., 0) / 40
    offset = np.full((n, 2), .5)
    if look >= 2 and pixels is not None:
        noise, _ = look_noise(pixels[0], pixels[1], k[8, 3]); offset += (noise - .5)[:, None] * k[6, 2:][None, :]

    def density(rho, cover):
        x = np.clip((rho - k[0, 0] - cover) * k[0, 1], 0, 1); return np.where(x > 0, x ** k[0, 2], 0.)

    def wave(x):
        t = x - np.floor(x) - .5; return t * (8 - 16 * np.abs(t))
    # c22.xyz of the fixture: the camera modulo the fine window (65536 units), centred, in float32.
    local = (origin - 65536. * np.floor(origin / 65536. + .5)).astype(np.float32).astype(np.float64)
    lit = np.zeros(n); lift = np.zeros(n); T = np.ones(n)
    visibility = 0. if shadowed else 1.
    for i in range(64):
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
        if look >= 2 and fogged.any():
            r = density(store.sample_level('far', points[fogged] + sun * k[7, 0], origin).astype(np.float64), cover[fogged]) * k[7, 1]
            tau = sigma * k[6, 0] * r
            sunward[fogged] = np.exp(-tau) * (1 - k[6, 1] * np.exp(-2 * (tau + sigma * k[6, 0] * rho[fogged] * k[7, 0])))
        vis = np.where(fogged, visibility, 1.)
        lit[active] += T[active] * a * sunward * (k[1, 3] + (1 - k[1, 3]) * vis)
        lift[active] += T[active] * a2 * (k[3, 3] + (1 - k[3, 3]) * vis)
        T[active] *= 1 - a
    cosine = d[:, 0] * sun[0] + d[:, 1] * sun[1] + d[:, 2] * sun[2]
    lobes = [k[4 + j, 2] / (k[4 + j, 0] - k[4 + j, 1] * cosine) ** 1.5 for j in range(2)]
    ambient = k[2, :3][None, :] + (k[3, :3] - k[2, :3])[None, :] * (.5 + .5 * cosine)[:, None]
    S = k[1, :3][None, :] * (((lobes[0] + lobes[1]) * lit + k[2, 3] * lift)[:, None] + ambient * (1 - T)[:, None])  # E/pi = 1
    return S.astype(m.F), T.astype(m.F)


def basis(pose):
    forward = np.asarray(pose['forward'], np.float64); up = np.asarray(pose['up'], np.float64)
    right = np.cross(up, forward); right /= np.linalg.norm(right); up = np.cross(forward, right)
    return right, up, forward


def both(origin, directions, limits, chroma, store):
    ev = m.filtered_eval(store)
    cand = m.integrate(origin, directions, limits, chroma, ev, 'candidate', 0)['full']
    dense = m.integrate(origin, directions, limits, chroma, ev, 'dense', 64.)['full']
    return {'candidate_S': cand['S'], 'candidate_T': cand['T'], 'dense64_S': dense['S'], 'dense64_T': dense['T']}


def run(asset_data, output):
    if output.exists():
        raise ValueError(f'output directory already exists: {output}')
    output.mkdir(parents=True)
    manifest = json.loads((asset_data / 'manifest.json').read_text())
    profile = next(x for x in manifest['profiles'] if x['name'] == 'foggreenoutlands')
    if abs(float(profile['base_sigma']) * 1.5 - m.SIGMA) > 1e-15:
        raise ValueError('sigma')
    volume = m.fog.decode_packet(asset_data / 'foggreenoutlands.fogbin', manifest)
    chroma = (volume[..., :3].sum((0, 1, 2), dtype=np.float64) / volume[..., 3].sum(dtype=np.float64)).astype(m.F)
    store = m.LazyStore(); arrays = {}; pops = m.populations()
    lines = ['sigma %r' % float(m.SIGMA), 'chroma %r %r %r' % tuple(float(np.float32(c)) for c in chroma)]

    def case(name, origin, pose, mode, value, extra=''):
        r, u, f = basis(pose)
        lines.append(' '.join([name] + [repr(float(v)) for v in (*origin, *r, *u, *f)] + [mode, value] + extra.split()))

    for pose in m.screen.pose_rows():
        name = pose['name']; origin = np.asarray(pose['origin'], np.float64); allr = m.rays(pose)
        combined = np.unique(np.concatenate([y * m.W + x for x, y in pops.values()]))
        sx, sy = pops['stratified']; wi = np.array([j * 32 + i for i, j in m.WITNESS]); wpix = sy[wi] * m.W + sx[wi]
        case(f'{name}_sky', origin, pose, 'sky', '0')
        arrays[f'{name}_sky_pixels'] = combined
        for k, v in both(origin, allr[combined], np.full(len(combined), m.FAR, m.F), chroma, store).items():
            arrays[f'{name}_sky_{k}'] = v
        t = math.tan(math.radians(30))
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
        for index, kind in enumerate(('zero', 'nan', 'inf')):
            case(f'{name}_invalid{index}', origin, pose, 'invalid', kind)
        case(f'{name}_empty', origin, pose, 'empty', '0')
        # Look presets on the stratified rays (half-resolution pixel = ray index): sky per preset, one geometry
        # depth across the taper, and the fully shadowed sky under L0 (black by construction) and L1 (coloured).
        strat = sy * m.W + sx; arrays[f'{name}_look_pixels'] = strat
        looks = [(f'{name}_look{look}_sky', look, 'sky', None, False) for look in (1, 2, 3)]
        if name == 'A':
            looks += [('A_look2_depth3', 2, 'depth', float(m.DEPTHS[3]), False), ('A_look1_depth90000', 1, 'depth', 90000., False), ('A_look0_shadowed', 0, 'sky', None, True), ('A_look1_shadowed', 1, 'sky', None, True)]
        for label, look, mode, depth, shadowed in looks:
            phase = LOOK_PHASE if look == 3 else 0
            case(label, origin, pose, mode, repr(depth) if depth else '0', f'look={look} phase={phase} shadow={int(shadowed)}')
            if look == 0:
                continue  # the fixture itself requires S == 0 exactly
            S, T = look_march(origin, allr[strat], np.full(len(strat), depth or m.FAR), depth is not None, chroma, store, look, m.SIGMA, phase, (sx, sy), shadowed)
            arrays[f'{label}_S'] = S; arrays[f'{label}_T'] = T
            if look == 3:
                arrays[f'{label}_noise_margin'] = look_noise(sx, sy, look_constants(3, chroma, phase=phase)[0][8, 3])[1]
    (output / 'cases.txt').write_text('\n'.join(lines) + '\n')
    np.savez(output / 'reference.npz', **arrays)
    record = dict(schema=1, width=m.W, height=m.H, shifts=SHIFTS, depths=m.DEPTHS.tolist(),
                  screen_sha256=m.digest(HERE / 'fog_density_runtime_screen.py'), exporter_sha256=m.digest(__file__),
                  cases_sha256=m.digest(output / 'cases.txt'), reference_sha256=m.digest(output / 'reference.npz'),
                  manifest_sha256=m.digest(asset_data / 'manifest.json'), packet_sha256=m.digest(asset_data / 'foggreenoutlands.fogbin'))
    (output / 'reference.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--asset-data', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); print(json.dumps(run(a.asset_data, a.output)))


if __name__ == '__main__':
    main()
