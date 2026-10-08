#!/usr/bin/env python3
"""Engine light on the hull: the detached GPU fixture (docs/architecture/engine-light.md).

Builds verification/probe/build/engine_light_fixture.exe (i686 MinGW, SSE2, four-byte stack) from
engine_light_fixture.cpp with the production renderer sources, runs it once in the selected bottle and validates its rows
against a float64 oracle written here, independently of the C++ core:

- constants: the uploaded c200-c202 against the record's law (light 0.5 x value behind the nozzle along the plume axis,
  radius 3 x value, colour = the cluster tint x I(s) x the preset scale x 0.25, I(s) = lerp(1.2, 4.0, s); the tints are
  parsed from src/proxy/engine_plumes_core.h) and the camera's forward axis;
- radiance: each sampled pixel of a tilted or facing plate (D3D9 pixel centres) intersected with the plate, the law
  E = colour x saturate(N . l) x saturate(1 - d^2 / R^2)^2 capped at 1 - decode(sum), the lit plate's radiance
  (decode(sum) + E)^(1 / 2.2) against the twin's within 1 %; pixels beyond the radius or facing away bit-identical to the
  light-less base variant; the cap case at 1.0; the depth target against the plate's depth law (pixel-centre witness);
- invariants: alpha, motion and depth bit-identical between base and twin, finite non-negative RGB; Reset: the
  same draw after a device Reset bit-identical;
- nozzle plates: the light-map term's contribution (white minus black light map, same program and constants) against
  g - (g - 1) w, w = max over the ship's main nozzles of saturate((r1^2 - (d_i / v_i)^2) / (r1^2 - r0^2)), d_i from the
  nozzle's light point (0.5 x value behind it), v_i its value (r0, r1 parsed from linear_engine_light_inc.h): one
  nozzle: the production twin at gain 4 <= 1.05 x the texel within r0 x value of the light (the d = 0 pixel 1.0), 4 x
  beyond r1 x value; the twin at gain 1 and the fill-only twin 1 x everywhere; the base without the light 4 x
  everywhere (the documented engine_light-off limit); three, eight, ten and 72 nozzles (twin3, twin8, twin10, twin72):
  1.0 at each nozzle's plate point and within r0 x its value, 4 x beyond r1 x value of every nozzle; the uploaded plate
  registers of the tier's run ((P_i - cam) / v_i, 1 / v_i), brightest first, pads (copies of slot 0) after the ship's
  plates, against the record law;
- a light per plate (DUAL rows): ships of two nozzles, "apart" (equal, 40 units apart beyond one reach 18: the Split
  Ocelot's head-on case) and "overlap" (6 and 4, 16 apart), of ten ("ten": the Ocelot's count, two of 6 and eight equal
  of 3.5) and of 72 ("cap": the plate cap); each pixel against the law with the light of the plate of the least
  (d / v)^2 (the uploaded registers; pixels within 1e-3 of the switch unclaimed), every plate's light (c200-c201 for
  plate 0, else its plate register's point and its colour register) against the record law, every plate lit, none
  dropped, each further nozzle's foot lit (where plate 0's light alone leaves it dark for "apart");
- cost: the per-draw CPU path (lookup, constants and plates, SetPixelShaderConstantF of the run and the light) <= 0.2
  us on a hit for one nozzle, and on ships of 8 and 72 nozzles <= 0.2 us x max(1, plates / 8); the ship table's build
  over 1,024 records (128 ships x 8 and 14 x 72 nozzles); the GPU cost of the term over a full-screen hull at 1920x1080
  and 5120x1440 for ships of 1, 3, 8, 10 and 72 nozzles (median of five EVENT-fenced batches).

Run as  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_engine_light.py
        [--wine-env CX_GRAPHICS_BACKEND=wined3d]
The compact record goes to verification/results/bottle-X3/engine-light-gpu.json; the raw report stays under
verification/probe/build/ (untracked). Never launches the game.
"""
import argparse
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle  # noqa: E402
import fixture_process  # noqa: E402

BUILD = ROOT / 'verification/probe/build'
EXE = BUILD / 'engine_light_fixture.exe'
SOURCES = ['verification/probe/engine_light_fixture.cpp', 'src/proxy/engine_light_core.h', 'src/renderer/linear_engine_light_inc.h',
           'src/renderer/linear_material.cpp', 'src/renderer/linear_material.h', 'src/renderer/material_motion.cpp',
           'src/proxy/engine_plumes_core.h', 'src/proxy/engine_effects_core.h']
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-static']
PROGRAMS = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
RELATIVE = 0.01          # the lit plate's radiance against the law, where it is visible ...
VISIBLE = 0.02           # ... (expected radiance >= 0.02; a darker sample is held to ABSOLUTE: the edge of the radius,
ABSOLUTE = 1e-3          # where q^2 ~ 1e-5, carries the interpolated eye vector's last bits)
HIT_NS_LIMIT = 200.0     # per-draw target (lookup + constants + upload)
CAP = 1.0
PLATE_NEAR_LIMIT = 1.05  # the gained light-map term within the full-suppression radius, x the texel
PLATE_ABSOLUTE = 0.01    # the contribution against g - (g - 1) w on the ramp (pixel-centre interpolation of v2)
PLATE_EXACT = 1e-5       # where the weight is 0 or 1, or the program has no weight
FAR_BAND = 1e-3          # the far region starts this far (relative) beyond r1 x value
PLATE_RUNS = (1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72)   # the tiers' runs (engine_light_core.h plate_runs)


def plate_tier(count):
    return next(k for k, run in enumerate(PLATE_RUNS) if run >= count or k == len(PLATE_RUNS) - 1)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    log = BUILD / 'engine-light-build.log'
    command = ['i686-w64-mingw32-g++', *FLAGS, 'verification/probe/engine_light_fixture.cpp', 'src/renderer/linear_material.cpp',
               'src/renderer/material_motion.cpp', '-o', str(EXE), '-ld3d9', '-luser32']
    with log.open('w') as out:
        subprocess.run(command, cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
    return len(re.findall(r'\bwarning:', log.read_text(errors='replace')))


def cluster_tints():
    text = (ROOT / 'src/proxy/engine_plumes_core.h').read_text()
    block = re.search(r'static const float tints\[ee::cluster_count\]\[3\] = \{(.*?)\};', text, re.S).group(1)
    tints = [tuple(float(v.rstrip('f')) for v in t.split(',')) for t in re.findall(r'\{([^{}]*)\}', block)]
    assert len(tints) == 13, 'thirteen cluster tints'
    return tints


def look():
    text = (ROOT / 'src/proxy/engine_plumes_core.h').read_text()
    low, high = re.search(r'float core_low = ([0-9.]+)f, core_high = ([0-9.]+)f;', text).groups()
    core = (ROOT / 'src/proxy/engine_light_core.h').read_text()
    behind, reach, scale = re.search(r'constexpr float behind = ([0-9.]+)f, reach = ([0-9.]+)f, colour_scale = ([0-9.]+)f;', core).groups()
    presets = re.search(r'\*out = p == Preset::restrained \? ([0-9.]+)f : p == Preset::strong \? ([0-9.]+)f : ([0-9.]+)f;', text).groups()
    return dict(core_low=float(low), core_high=float(high), behind=float(behind), reach=float(reach), colour_scale=float(scale),
                preset={0: float(presets[0]), 1: float(presets[2]), 2: float(presets[1])})


def plate_radii():
    text = (ROOT / 'src/renderer/linear_engine_light_inc.h').read_text()
    reach, full, zero = re.search(r'constexpr float engine_light_reach_ratio = ([0-9.]+)f, nozzle_plate_full = ([0-9.]+)f, '
                                  r'nozzle_plate_reach = ([0-9.]+)f;', text).groups()
    return float(reach), float(full), float(zero)


def expected_constants(case, tints, k):
    value = case['value']
    light = [case['nozzle'][i] + case['axis'][i] * k['behind'] * value for i in range(3)]
    rel = [light[i] - case['camera'][i] for i in range(3)]
    intensity = k['core_low'] + (k['core_high'] - k['core_low']) * min(max(case['s'], 0.0), 1.0)
    colour = [tints[case['cluster']][i] * intensity * k['preset'][case['preset']] * k['colour_scale'] for i in range(3)]
    radius = k['reach'] * value
    return rel + [radius * radius] + colour + [1.0 / (radius * radius)] + [0.0, 0.0, 1.0, 0.0]


def parse(text):
    cases, invariants, samples, plates, duals = {}, {}, {}, {}, {}
    out = dict(programs=[], create=None, reset=None, cost=None, gpu=[], done='DONE' in text.split(),
               teardown=[line for line in text.splitlines() if line.startswith('TEARDOWN ')])
    for line in text.splitlines():
        if line.startswith('P '):
            p = line.split()
            plates[(int(p[1]), p[2])]['rows'].append((int(p[3]), int(p[4]), [float(v) for v in p[5:8]]))
            continue
        if line.startswith('S '):
            p = line.split()
            samples.setdefault(int(p[1]), []).append((int(p[2]), int(p[3]), [float(v) for v in p[4:7]], [float(v) for v in p[7:10]],
                                                      float(p[10]), p[11] == '1'))
            continue
        fields = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        if line.startswith('CASE '):
            nums = lambda s: [float(v) for v in s.split(',')]
            cases[int(fields['id'])] = dict(pair=int(fields['pair']), kind=int(fields['kind']), tilt=float(fields['tilt']),
                                            emissive=float(fields['emissive']), fp16=fields['fp16'] == '1', size=int(fields['size']),
                                            nozzle=nums(fields['nozzle']), axis=nums(fields['axis']), value=float(fields['value']),
                                            s=float(fields['s']), cluster=int(fields['cluster']), preset=int(fields['preset']),
                                            light=nums(fields['light']), camera=nums(fields['camera']), m22=float(fields['m22']),
                                            m32=float(fields['m32']))
        elif line.startswith('PLATE '):
            plates[(int(fields['pair']), fields['mode'])] = dict(
                twin=fields['twin'] == '1', gain=float(fields['gain']), value=float(fields['value']),
                finite_bad=int(fields['finite_bad']), light=[float(v) for v in fields['light'].split(',')],
                nozzles=[[float(v) for v in n.split(',')] for n in fields['nozzles'].split(';')],
                registers=[float(v) for v in fields['plates'].split(',')], rows=[])
        elif line.startswith('DUAL '):
            nums = lambda s: [float(v) for v in s.split(',')]
            keys = nums(fields['keys'])
            duals[int(fields['id'])] = dict(pair=int(fields['pair']), kind=int(fields['kind']), name=fields['name'],
                                            size=int(fields['size']), cluster=int(fields['cluster']), s=float(fields['s']),
                                            preset=int(fields['preset']), count=int(fields['count']),
                                            plates=int(fields['plates']), plates_dropped=int(fields['plates_dropped']),
                                            nozzles=[nums(n) for n in fields['nozzles'].split(';')],
                                            lights=[nums(n) for n in fields['lights'].split(';')],
                                            keys=[keys[i:i + 4] for i in range(0, len(keys), 4)], tier=float(fields['tier']),
                                            invariants={n: int(fields[n]) for n in ('alpha_bad', 'motion_bad', 'depth_bad', 'finite_bad')})
        elif line.startswith('INVARIANT '):
            invariants[int(fields['id'])] = {k: int(v) for k, v in fields.items() if k != 'id'}
        elif line.startswith('PROGRAM '):
            out['programs'].append(fields)
        elif line.startswith('CREATE '):
            out['create'] = {k: float(v) for k, v in fields.items()}
        elif line.startswith('RESET '):
            out['reset'] = {k: int(v) for k, v in fields.items()}
        elif line.startswith('COST '):
            out['cost'] = {k: float(v) for k, v in fields.items()}
        elif line.startswith('COSTN '):
            out.setdefault('cost_plates', []).append({k: float(v) for k, v in fields.items()})
        elif line.startswith('GPU '):
            out['gpu'].append(dict(width=int(fields['width']), height=int(fields['height']), nozzles=int(fields.get('nozzles', 1)),
                                   base_ms=[float(v) for v in fields['base_ms'].split(',')],
                                   twin_ms=[float(v) for v in fields['twin_ms'].split(',')]))
    out.update(cases=cases, invariants=invariants, samples=samples, plates=plates, duals=duals)
    return out


def expected_plates(plate, k):
    """The plate registers of the tier's run from the record law: per nozzle (x, y, z camera-relative, value; axis +z)
    the light point 0.5 x value behind it, ((P - cam) / v, 1 / v), brightest first (s 1: by value; equal values in
    record order, the lower handle), then pads, copies of slot 0, to the run (1, 4, 8, 12, 16, 24, ... 72 by the
    count)."""
    nozzles = sorted(plate['nozzles'], key=lambda n: -n[3])[:PLATE_RUNS[-1]]
    out = []
    for x, y, z, v in nozzles:
        point = (x, y, z + k['behind'] * v)
        out += [point[0] / v, point[1] / v, point[2] / v, 1.0 / v]
    run = PLATE_RUNS[plate_tier(len(nozzles))]
    return out + out[:4] * (run - len(nozzles))


def oracle_plate(plate, size, radii, k):
    """The light-map term's contribution per sampled pixel against g_eff = g - (g - 1) w, w the maximum over the ship's
    nozzles; a twin of a gained kind carries the weight, the base and the fill-only twin do not (their g_eff is the
    uploaded gain or 1). Near: within r0 x value of some nozzle's light point; far: beyond r1 x value of every one by
    more than FAR_BAND (a sample on the boundary carries the interpolated eye vector's last bits: the ramp's tolerance;
    the 72-nozzle ship puts two samples within 1e-4 of it)."""
    reach, r0, r1 = radii
    points = [((x, y, z + k['behind'] * v), v) for x, y, z, v in plate['nozzles']]
    stats = dict(samples=len(plate['rows']), near=0, near_max=0.0, near_min=math.inf, centre=None, far=0, far_min=math.inf,
                 far_max=0.0, ramp=0, max_error=0.0, channel_spread=0.0, nozzles=len(points),
                 per_nozzle=[dict(near=0, near_max=0.0, nearest=math.inf, at_point=None) for _ in points])
    expected_registers = expected_plates(plate, k)
    stats['register_error'] = (max(abs(a - b) / max(1.0, abs(b)) for a, b in zip(plate['registers'], expected_registers))
                               if len(plate['registers']) == len(expected_registers) else math.inf)
    for x, y, rgb in plate['rows']:
        ndc = (2.0 * x / size - 1.0, 1.0 - 2.0 * y / size)
        point = (50.0 * ndc[0], 50.0 * ndc[1], 50.0)
        dvs = [math.sqrt(sum((point[i] - p[i]) ** 2 for i in range(3))) / v for p, v in points]
        weight = max(min(max((r1 * r1 - dv * dv) / (r1 * r1 - r0 * r0), 0.0), 1.0) for dv in dvs)
        mode = plate['mode']
        g = plate['gain'] if mode.startswith('twin') or mode in ('gain1', 'nolight') else 1.0
        expected = g - (g - 1.0) * weight if mode.startswith('twin') or mode == 'gain1' else g
        c = rgb[0]
        stats['channel_spread'] = max(stats['channel_spread'], max(rgb) - min(rgb))
        stats['max_error'] = max(stats['max_error'], abs(c - expected))
        if x == size // 2 and y == size // 2:
            stats['centre'] = c
        for i, dv in enumerate(dvs):
            n = stats['per_nozzle'][i]
            if dv <= r0:
                n['near'] += 1
                n['near_max'] = max(n['near_max'], c)
            if dv < n['nearest']:
                n['nearest'], n['at_point'] = dv, c
        if min(dvs) <= r0:
            stats['near'] += 1
            stats['near_max'], stats['near_min'] = max(stats['near_max'], c), min(stats['near_min'], c)
        elif min(dvs) >= r1 * (1.0 + FAR_BAND):
            stats['far'] += 1
            stats['far_min'], stats['far_max'] = min(stats['far_min'], c), max(stats['far_max'], c)
        else:
            stats['ramp'] += 1
    return stats


def oracle_case(case, rows, constants):
    """Per sampled pixel: the plate point, the law, the expected lit radiance; returns the case's statistics."""
    size, tilt = case['size'], case['tilt']
    normal = (-math.sin(tilt), 0.0, -math.cos(tilt))           # R_y(tilt) (0, 0, -1)
    p0 = (0.0, 0.0, 50.0)                                      # the plate's origin relative to the camera
    rel, r2, colour = constants[0:3], constants[3], constants[4:7]
    stats = dict(samples=len(rows), plate=0, lit=0, visible=0, max_relative=0.0, worst=None, max_dark_absolute=0.0, zero=0,
                 zero_not_identical=0, capped=0, cap_max_error=0.0, max_depth_error=0.0, energy_checked=0,
                 max_energy_relative=0.0)
    for x, y, base, twin, depth, identical in rows:
        ndc = (2.0 * x / size - 1.0, 1.0 - 2.0 * y / size)
        direction = (ndc[0], ndc[1], 1.0)                      # m00 = m11 = 1
        denom = sum(normal[i] * direction[i] for i in range(3))
        if abs(denom) < 1e-12:
            continue
        t = sum(normal[i] * p0[i] for i in range(3)) / denom
        point = [t * direction[i] for i in range(3)]
        local = [point[i] - p0[i] for i in range(3)]
        model_x = math.cos(tilt) * local[0] - math.sin(tilt) * local[2]   # R^T: model x, y within the plate
        if abs(model_x) > 119.0 or abs(local[1]) > 119.0 or t <= 0:
            continue
        stats['plate'] += 1
        w = point[2]
        stats['max_depth_error'] = max(stats['max_depth_error'], abs(depth - (case['m22'] + case['m32'] / w)))
        to = [rel[i] - point[i] for i in range(3)]
        d2 = sum(v * v for v in to)
        q = min(max(1.0 - d2 / r2, 0.0), 1.0)
        ndl = min(max(sum(normal[i] * to[i] for i in range(3)) / math.sqrt(d2), 0.0), 1.0) if d2 > 0 else 0.0
        energy = [colour[i] * ndl * q * q for i in range(3)]
        if d2 > r2 * 1.004 or ndl == 0.0:
            stats['zero'] += 1
            stats['zero_not_identical'] += 0 if identical else 1
            continue
        if d2 > r2 * 0.996:
            continue                                           # the pixel-centre band at the radius: no claim
        stats['lit'] += 1
        for c in range(3):
            decoded = max(base[c], 0.0) ** 2.2
            capped = min(energy[c], max(0.0, CAP - decoded))
            expected = (decoded + capped) ** (1 / 2.2)
            if expected < VISIBLE:
                stats['max_dark_absolute'] = max(stats['max_dark_absolute'], abs(twin[c] - expected))
                continue
            stats['visible'] += 1
            relative = abs(twin[c] - expected) / expected
            if relative > stats['max_relative']:
                stats['max_relative'] = relative
                stats['worst'] = dict(x=x, y=y, channel=c, twin=twin[c], expected=expected, base=base[c], energy=energy[c])
            if energy[c] > CAP - decoded:
                stats['capped'] += 1
                stats['cap_max_error'] = max(stats['cap_max_error'], abs(twin[c] - 1.0))
            if capped >= 0.02:
                stats['energy_checked'] += 1
                measured = twin[c] ** 2.2 - decoded
                stats['max_energy_relative'] = max(stats['max_energy_relative'], abs(measured - capped) / capped)
    return stats


def expected_dual(dual, tints, k):
    """Every plate's light and plate register from the record law (axis +z, plate order = record order: the brighter
    first, the brightness tie's lower handle first): per light (L - cam, R^2, colour, 1 / R^2), per plate
    ((P - cam) / v, 1 / v)."""
    lights, keys = [], []
    intensity = k['core_low'] + (k['core_high'] - k['core_low']) * min(max(dual['s'], 0.0), 1.0)
    colour = [tints[dual['cluster']][i] * intensity * k['preset'][dual['preset']] * k['colour_scale'] for i in range(3)]
    for x, y, z, v in dual['nozzles']:
        point = (x, y, z + k['behind'] * v)
        radius = k['reach'] * v
        lights.append(list(point) + [radius * radius] + colour + [1.0 / (radius * radius)])
        keys.append([point[0] / v, point[1] / v, point[2] / v, 1.0 / v])
    return lights, keys


def oracle_dual(dual, rows):
    """A light per plate: per sampled pixel of the facing plate (tilt 0), the plate of the least (d / v)^2 from the
    uploaded plate registers (ties: the earlier plate), then the law with that plate's light (as oracle_case); pixels
    within 1e-3 relative of the selection's switch between the two least are not claimed (seam). Per light: the lit
    samples, the twin at the sample nearest the light's foot on the plate, and what plate 0's light alone (the rule
    before) gives there."""
    size = dual['size']
    lights, keys = dual['lights'], dual['keys']
    n = len(lights)
    normal = (0.0, 0.0, -1.0)
    stats = dict(samples=len(rows), lit=[0] * n, visible=0, max_relative=0.0, worst=None, max_dark_absolute=0.0, zero=0,
                 zero_not_identical=0, seam=0, at_foot=[None] * n, foot_distance=[math.inf] * n, before_at_foot=[None] * n)

    def law(light, point):
        rel, r2, colour = light[0:3], light[3], light[4:7]
        to = [rel[i] - point[i] for i in range(3)]
        d2 = sum(v * v for v in to)
        q = min(max(1.0 - d2 / r2, 0.0), 1.0)
        ndl = min(max(sum(normal[i] * to[i] for i in range(3)) / math.sqrt(d2), 0.0), 1.0) if d2 > 0 else 0.0
        return d2, r2, ndl, [colour[i] * ndl * q * q for i in range(3)]

    for x, y, base, twin, depth, identical in rows:
        ndc = (2.0 * x / size - 1.0, 1.0 - 2.0 * y / size)
        point = (50.0 * ndc[0], 50.0 * ndc[1], 50.0)
        u = [sum((point[i] * key[3] - key[i]) ** 2 for i in range(3)) for key in keys]
        for i in range(n):
            foot = math.hypot(point[0] - lights[i][0], point[1] - lights[i][1])
            if foot < stats['foot_distance'][i]:
                stats['foot_distance'][i] = foot
                stats['at_foot'][i] = twin[0]
                stats['before_at_foot'][i] = law(lights[0], point)[3][0]
        order = sorted(range(n), key=lambda i: (u[i], i))
        if n > 1 and abs(u[order[0]] - u[order[1]]) <= 1e-3 * max(u[order[0]], u[order[1]]):
            stats['seam'] += 1
            continue
        chosen = order[0]
        d2, r2, ndl, energy = law(lights[chosen], point)
        if d2 > r2 * 1.004 or ndl == 0.0:
            stats['zero'] += 1
            stats['zero_not_identical'] += 0 if identical else 1
            continue
        if d2 > r2 * 0.996:
            continue
        stats['lit'][chosen] += 1
        for c in range(3):
            decoded = max(base[c], 0.0) ** 2.2
            expected = (decoded + min(energy[c], max(0.0, CAP - decoded))) ** (1 / 2.2)
            if expected < VISIBLE:
                stats['max_dark_absolute'] = max(stats['max_dark_absolute'], abs(twin[c] - expected))
                continue
            stats['visible'] += 1
            relative = abs(twin[c] - expected) / expected
            if relative > stats['max_relative']:
                stats['max_relative'] = relative
                stats['worst'] = dict(x=x, y=y, channel=c, twin=twin[c], expected=expected, light=chosen)
    return stats


def validate(report):
    tints, k = cluster_tints(), look()
    parsed = parse(report)
    assert parsed['done'], 'fixture did not finish'
    result = dict(cases={}, programs=parsed['programs'], create=parsed['create'], reset=parsed['reset'], cost=parsed['cost'],
                  teardown=parsed['teardown'])
    for cid, case in sorted(parsed['cases'].items()):
        expected = expected_constants(case, tints, k)
        constant_error = max(abs(a - b) / max(1.0, abs(b)) for a, b in zip(case['light'], expected))
        stats = oracle_case(case, parsed['samples'][cid], case['light'])
        inv = parsed['invariants'][cid]
        tolerance = RELATIVE
        checks = dict(constants=constant_error <= 1e-5, radiance=stats['max_relative'] <= tolerance and stats['visible'] > 0,
                      dark=stats['max_dark_absolute'] <= ABSOLUTE,
                      zero=stats['zero_not_identical'] == 0 and stats['zero'] > 0, lit=stats['lit'] > 0,
                      depth=stats['max_depth_error'] <= 2e-6,
                      invariants=inv['alpha_bad'] == 0 and inv['motion_bad'] == 0 and inv['depth_bad'] == 0 and inv['finite_bad'] == 0)
        if case['preset'] == 2:
            checks['cap'] = stats['capped'] > 0 and stats['cap_max_error'] <= (2e-3 if case['fp16'] else 1e-5)
        result['cases'][cid] = dict(pair=case['pair'], kind=case['kind'], tilt=case['tilt'], emissive=case['emissive'], fp16=case['fp16'],
                                    constant_error=constant_error, stats=stats, invariants=inv, checks=checks,
                                    passed=all(checks.values()))
    # A light per plate: every plate lit with its own light, the per-pixel selection, the rest of the plate untouched,
    # no plate dropped (ships of 2, 10 and 72 nozzles).
    result['dual'] = {}
    for did, dual in sorted(parsed['duals'].items()):
        lights, keys = expected_dual(dual, tints, k)
        flat = lambda rows: [v for row in rows for v in row]
        complete = len(dual['lights']) == len(lights) and len(dual['keys']) == len(keys)
        constant_error = (max(abs(a - b) / max(1.0, abs(b)) for a, b in zip(flat(dual['lights'] + dual['keys']), flat(lights + keys)))
                          if complete else math.inf)
        stats = oracle_dual(dual, parsed['samples'][did])
        checks = dict(constants=constant_error <= 1e-5, tier=dual['tier'] == float(plate_tier(dual['count'])),
                      plates=dual['plates'] == dual['count'] and dual['plates_dropped'] == 0,
                      radiance=stats['max_relative'] <= RELATIVE and stats['visible'] > 0,
                      dark=stats['max_dark_absolute'] <= ABSOLUTE,
                      zero=stats['zero_not_identical'] == 0 and stats['zero'] > 0,
                      all_lit=all(n > 0 for n in stats['lit']),
                      every_nozzle=all(f is not None and f > 0.0 for f in stats['at_foot']),
                      invariants=all(v == 0 for v in dual['invariants'].values()))
        if dual['name'] == 'apart':
            # The two nozzles beyond one reach: plate 0's light alone leaves the second nozzle's plate dark.
            checks['before_dark'] = stats['before_at_foot'][1] == 0.0
        result['dual']['%d-%s-%d' % (dual['pair'], dual['name'], dual['kind'])] = dict(
            constant_error=constant_error, stats=stats, invariants=dual['invariants'], checks=checks,
            passed=all(checks.values()))
    radii = plate_radii()
    result['plate'] = dict(radii=dict(reach=radii[0], full=radii[1], zero=radii[2]), modes={})
    for (pair, mode), plate in sorted(parsed['plates'].items()):
        plate['mode'] = mode
        stats = oracle_plate(plate, 256, radii, k)
        exact = PLATE_ABSOLUTE if mode.startswith('twin') else PLATE_EXACT
        checks = dict(finite=plate['finite_bad'] == 0, samples=stats['near'] > 0 and stats['far'] > 0 and stats['ramp'] > 0,
                      law=stats['max_error'] <= exact, grey=stats['channel_spread'] <= PLATE_EXACT,
                      registers=stats['register_error'] <= 1e-5)
        if mode == 'twin':
            checks.update(near=stats['near_max'] <= PLATE_NEAR_LIMIT, centre=abs(stats['centre'] - 1.0) <= PLATE_EXACT,
                          far=abs(stats['far_min'] - 4.0) <= PLATE_EXACT and abs(stats['far_max'] - 4.0) <= PLATE_EXACT)
        elif mode in ('twin3', 'twin8', 'twin10', 'twin72'):
            # Every nozzle at gain 1: its near samples and the sample nearest its plate point at the texel's 1.0.
            checks.update(nozzles=stats['nozzles'] == int(mode[4:]),
                          each_near=all(n['near'] > 0 and n['near_max'] <= PLATE_NEAR_LIMIT for n in stats['per_nozzle']),
                          each_point=all(abs(n['at_point'] - 1.0) <= PLATE_EXACT for n in stats['per_nozzle']),
                          far=abs(stats['far_min'] - 4.0) <= PLATE_EXACT and abs(stats['far_max'] - 4.0) <= PLATE_EXACT)
        elif mode == 'nolight':
            checks.update(near=abs(stats['near_min'] - 4.0) <= PLATE_EXACT and abs(stats['near_max'] - 4.0) <= PLATE_EXACT)
        else:
            checks.update(near=abs(stats['near_max'] - 1.0) <= PLATE_EXACT, far=abs(stats['far_max'] - 1.0) <= PLATE_EXACT)
        result['plate']['modes']['%d-%s' % (pair, mode)] = dict(stats=stats, checks=checks, passed=all(checks.values()))
    result['plate']['passed'] = len(result['plate']['modes']) == 16 and all(m['passed'] for m in result['plate']['modes'].values())
    cost = parsed['cost']
    result['cost_checks'] = dict(hit_ns=cost['hit_ns'] <= HIT_NS_LIMIT, hits=cost['hits'] == cost['rounds'], misses=cost['misses'] == cost['rounds'])
    # Ships of 8 and 72 nozzles: the per-draw path grows with the plates in use, bounded proportionally.
    result['cost_plates'] = parsed.get('cost_plates', [])
    result['cost_checks']['plates'] = (sorted(int(c['plates']) for c in result['cost_plates']) == [8, PLATE_RUNS[-1]] and
                                       all(c['hits'] == c['rounds'] and
                                           c['hit_ns'] <= HIT_NS_LIMIT * max(1.0, c['plates'] / 8.0)
                                           for c in result['cost_plates']))
    result['gpu'] = []
    for g in parsed['gpu']:
        base, twin = statistics.median(g['base_ms']), statistics.median(g['twin_ms'])
        result['gpu'].append(dict(width=g['width'], height=g['height'], nozzles=g['nozzles'], base_ms=base, twin_ms=twin,
                                  term_us=(twin - base) * 1e3,
                                  base_batches=g['base_ms'], twin_batches=g['twin_ms']))
    result['reset_ok'] = parsed['reset'] is not None and parsed['reset']['differ'] == 0
    # The fixture's teardown: every object released, the device's last Release at zero references, then done (a leaked
    # twin, pass or surface keeps references above zero and fails the run).
    teardown = parsed['teardown']
    result['teardown_ok'] = 'TEARDOWN device references=0' in teardown and 'TEARDOWN done' in teardown
    result['passed'] = (all(c['passed'] for c in result['cases'].values()) and all(result['cost_checks'].values()) and
                        result['plate']['passed'] and len(result['dual']) == 8 and
                        all(d['passed'] for d in result['dual'].values()) and
                        result['reset_ok'] and result['teardown_ok'] and len(result['gpu']) == 10 and len(result['cases']) >= 9)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=float, default=420)
    parser.add_argument('--wine-env', action='append', default=[], metavar='NAME=VALUE',
                        help="Pass a variable through CrossOver's `wine --env` (applied after the bottle's environment), e.g. "
                             'CX_GRAPHICS_BACKEND=wined3d or =dxvk to select the builtin d3d9 for one run')
    args = parser.parse_args()
    for item in args.wine_env:
        if '=' not in item or not item.split('=', 1)[0]:
            parser.error(f'--wine-env expects NAME=VALUE: {item}')
    if bottle.BOTTLE != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    warnings = None if args.no_build else build()
    report_path = BUILD / 'engine-light-report.txt'
    command = [bottle.WINE, *bottle.wine_args(), *[a for item in args.wine_env for a in ('--env', item)], '--dll', 'd3d9=b',
               str(EXE), 'Z:' + str(PROGRAMS)]
    started = time.time()
    exit_code, timeout = None, None
    with report_path.open('w') as out, (BUILD / 'engine-light-wine.log').open('w') as err:
        try:
            exit_code = fixture_process.run(command, build_dir=BUILD, stdout=out, stderr=err,
                                            env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=args.timeout).returncode
        except fixture_process.FixtureTimeout as error:
            timeout = str(error)
    elapsed = time.time() - started
    report = report_path.read_text(errors='replace')
    record = dict(passed=False, bottle=bottle.describe(), wine_env=list(args.wine_env), game_launched=False, exit_code=exit_code, timeout=timeout,
                  elapsed_s=round(elapsed, 2), build_warnings=warnings, executable_sha256=sha(EXE),
                  sources_sha256={s: sha(ROOT / s) for s in SOURCES}, raw_report=str(report_path.relative_to(ROOT)))
    try:
        assert exit_code == 0, 'fixture exit %s, timeout %s (see %s)' % (exit_code, timeout, report_path)
        record.update(validate(report))
    except BaseException as error:
        record['error'] = repr(error)
    path = bottle.results_dir(ROOT) / 'engine-light-gpu.json'
    path.write_text(json.dumps(record, indent=1, sort_keys=True) + '\n')
    summary = dict(passed=record['passed'], error=record.get('error'), cost=record.get('cost'),
                   cost_plates=record.get('cost_plates'), gpu=[{k: g[k] for k in ('width', 'height', 'nozzles', 'base_ms', 'twin_ms', 'term_us')} for g in record.get('gpu', [])],
                   cases={cid: dict(passed=c['passed'], max_relative=round(c['stats']['max_relative'], 6), lit=c['stats']['lit'], zero=c['stats']['zero'],
                                    capped=c['stats']['capped'], depth=c['stats']['max_depth_error'], checks=[n for n, v in c['checks'].items() if not v])
                          for cid, c in record.get('cases', {}).items()},
                   plate={k: dict(passed=m['passed'], centre=m['stats']['centre'], near_max=m['stats']['near_max'],
                                  near_min=m['stats']['near_min'], far_min=m['stats']['far_min'], far_max=m['stats']['far_max'],
                                  max_error=m['stats']['max_error'], register_error=m['stats']['register_error'],
                                  nozzles=m['stats']['nozzles'],
                                  at_point_min=min((n['at_point'] for n in m['stats']['per_nozzle'] if n['at_point'] is not None), default=None),
                                  at_point_max=max((n['at_point'] for n in m['stats']['per_nozzle'] if n['at_point'] is not None), default=None),
                                  checks=[n for n, v in m['checks'].items() if not v])
                          for k, m in record.get('plate', {}).get('modes', {}).items()},
                   dual={k: dict(passed=d['passed'], plates=len(d['stats']['lit']),
                                 plates_lit=sum(1 for n in d['stats']['lit'] if n > 0), min_lit=min(d['stats']['lit']),
                                 max_relative=round(d['stats']['max_relative'], 6),
                                 at_foot_min=min((f for f in d['stats']['at_foot'] if f is not None), default=None),
                                 before_at_foot=d['stats']['before_at_foot'][:2],
                                 seam=d['stats']['seam'], checks=[n for n, v in d['checks'].items() if not v])
                         for k, d in record.get('dual', {}).items()},
                   reset=record.get('reset'), create=record.get('create'), teardown=record.get('teardown'),
                   teardown_ok=record.get('teardown_ok'), record=str(path.relative_to(ROOT)))
    print(json.dumps(summary, indent=1))
    raise SystemExit(0 if record['passed'] else 1)


if __name__ == '__main__':
    main()
