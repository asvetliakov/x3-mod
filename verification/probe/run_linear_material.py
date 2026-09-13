#!/usr/bin/env python3
"""Detached GPU qualification; consumes an explicitly built EXE, never rebuilds.

Invoke under wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Raw output/cases stay in
/tmp. The compact results record contains oracle tolerances and scoped coverage.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import os
import re
import statistics
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle
from game_guard import game_running
import linear_material_reference as ref

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
EXE = ROOT / 'verification/probe/build/linear_material_fixture.exe'
CODE_INPUTS = ('src/renderer/linear_material.cpp', 'src/renderer/linear_material.h',
               'src/renderer/material_motion.cpp', 'src/renderer/material_motion.h',
               'src/renderer/motion_output_profiles.h',
               'src/renderer/motion_output_profiles_inc.h',
               'docs/reverse-engineering/linear-material-profiles.json',
               'verification/probe/linear_material_fixture.cpp',
               'verification/probe/linear_material_reference.py',
               'verification/probe/run_linear_material.py')
PAIRS = [('53a0a641107ed76c', ps) for ps in ('63f96eba9eea7880', '8759c7838bbc86c2')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('593e5dea9b3457d5', '7a0bb00a8070496a', '8d5b2ba0fb4d13bf', 'dab93928f26906f7')]
PAIRS += [('53a0a641107ed76c', ps) for ps in ('3b94320087e81945', 'e3b7acc16da9932d')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('7a14d4dcb28f27e5', '8ab6188a40ca15ea', '8df6143d0e77d92e', 'e16a9806ee3544c3')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('ca6bfa4a6cca7e2a', '5e0a10fe752b6140')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('63379470db8d2a86', '68915563dd0aac9a', 'd086fde54698070c', 'f17fffd88d134b04')]
TIMING_PAIRS = (0, 10, 20)
CUBE_PATTERN, FOG, BOUNDARY = 1, 2, 4
CUBE_BANDS_U = (.125, .25, .25, .5)
CUBE_BANDS_V = (.125, .375, .375, .75)


def family_name(pair):
    return 'Argon BUMP' if pair >= 20 else 'shared DEFAULT' if pair >= 10 else 'Argon'


def cube_location(direction):
    # D3D9 LookAtLH face basis, followed by viewport Y inversion:
    # https://learn.microsoft.com/en-us/windows/win32/direct3d9/creating-cubic-environment-map-surfaces
    x, y, z = direction
    magnitude = max(abs(x), abs(y), abs(z))
    if not math.isfinite(magnitude) or magnitude == 0:
        raise ValueError('analytical cube direction must be finite and nonzero')
    if abs(x) == magnitude:
        face, u, v = (0, -z, -y) if x > 0 else (1, z, -y)
    elif abs(y) == magnitude:
        face, u, v = (2, x, z) if y > 0 else (3, x, -z)
    else:
        face, u, v = (4, x, -y) if z > 0 else (5, -x, -y)
    return face, .5 * (u / magnitude + 1), .5 * (v / magnitude + 1)


def cube_sample(direction, require_margin=True):
    face, u, v = cube_location(direction)
    # The central 2x2 texels share a color, so exact axis directions do not
    # straddle a color discontinuity. Other samples stay clear of .25/.75.
    if require_margin:
        xyz = sorted(map(abs, direction))
        if xyz[1] / xyz[2] > .9 or min(abs(t-edge) for t in (u,v) for edge in (.25,.75)) < .025:
            raise ValueError('cube witness too close to face or color boundary')
    ix, iy = min(3, max(0, int(u * 4))), min(3, max(0, int(v * 4)))
    return ((face + 1) / 8., CUBE_BANDS_U[ix], CUBE_BANDS_V[iy])
AFFINE = ((.75, .125, 0, .0625), (0, .5, .25, .03125), (.125, 0, .875, -.03125))
# Retained _pp angular arithmetic may round intermediate lobes. This tolerance
# is independent of half-target storage and is not permission to clamp HDR.
RGB_REL_TOL = .006
RGB_ABS_TOL = 2e-5


def fixture_cases():
    cases = []
    base = dict(pair=0, depth=1, lights=1, reverse=0, affine=0, valid=1, fp16=1,
                gains=[1., 1., 1.], diffuse=[.5, .25, .75, .75],
                lightmap=[.125, .25, .0625, .25], cube=[.25, .5, .125, 1.],
                mask=.25, material=[.25, .125, .0625], point=[.5, .25, .125],
                dir0=[.375, .25, .5], dir1=[.125, .5, .25], normal=[0., 0., 1.], glow=.25, flags=0,
                normal_sample=[.25, .5, .75, .5], binormal=[0., 1., 0.],
                tangent=[1., 0., 0.], camera=[0., 0., 4.], fog_clip=[.75, .125])

    def add(label, **kwargs):
        c = copy.deepcopy(base)
        c.update(copy.deepcopy(kwargs))
        c.update(id=len(cases), label=label)
        cases.append(c)

    for pair in range(10):
        for depth in (0, 1):
            for reverse in (0, 1):
                add('pair_depth_face', pair=pair, depth=depth, reverse=reverse)
    for pair in (0, 2, 6):
        for lights in ((0, 1, 8) if pair != 6 else (1,)):
            for gain in (1., 4., 16.):
                add('lights_gains', pair=pair, lights=lights, gains=[gain] * 3)
    for pair in range(6):
        add('affine_angular', pair=pair, affine=1, normal=[.6, 0., .8])
        for source in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube'):
            isolated = {name: ([0.] * 3 if name not in ('lightmap', 'cube') else [0., 0., 0., 1.])
                        for name in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube')}
            isolated[source] = base[source]
            add('isolated_' + source, pair=pair, **isolated)
    # Independent gains prevent a misplaced shared multiplier from passing.
    for gains in ([16., 1., 1.], [1., 16., 1.], [1., 1., 16.], [0., 0., 0.]):
        add('independent_gains', gains=gains)
    for amplitude in (0., 1., 4., 16., 256.):
        add('native_material_strength', pair=2, material=[amplitude, amplitude / 2, amplitude / 4],
            point=[0.] * 3, dir0=[0.] * 3, lightmap=[0., 0., 0., .25], cube=[0., 0., 0., 1.])
    # Isolate each converted input: NaN/Inf arithmetic in another legacy source
    # or affine 0*Inf is deliberately avoided so sanitizer behavior is observable.
    safety = (('black', 0.), ('minus_zero', -0.), ('negative', -1.), ('tiny', 1e-8),
              ('nan', math.nan), ('posinf', math.inf), ('neginf', -math.inf),
              ('cap', ref.CAP), ('overcap', 1e7))
    for source in ('diffuse', 'point', 'material', 'dir0', 'lightmap', 'cube'):
        for name, value in safety:
            isolated = dict(diffuse=[1., 1., 1., .75], point=[0.] * 3,
                            material=[0.] * 3, dir0=[0.] * 3, dir1=[0.] * 3,
                            lightmap=[0., 0., 0., .25], cube=[0., 0., 0., 1.], mask=1.)
            isolated[source] = [value] * 3 + (isolated[source][3:] if source in ('diffuse', 'lightmap', 'cube') else [])
            if source == 'diffuse': isolated['material'] = [1.] * 3
            add('safety_' + source + '_' + name, pair=2, fp16=0, **isolated)
    add('missing_history', valid=0)
    # Keep the first slice's 167 cases/IDs unchanged. The transfer implementation
    # is shared, so its 54 exceptional-source cases above run once, not per family.
    for pair in range(10, 20):
        for depth in (0, 1):
            for reverse in (0, 1):
                add('pair_depth_face', pair=pair, depth=depth, reverse=reverse)
    for pair in (10, 12, 16):
        for lights in ((1,) if pair == 16 else (0, 1, 8)):
            for gain in (1., 4., 16.):
                add('lights_gains', pair=pair, lights=lights, gains=[gain] * 3)
    for pair in range(10, 16):
        add('affine_angular', pair=pair, affine=1, normal=[.6, 0., .8])
        for source in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube'):
            isolated = {name: ([0.] * 3 if name not in ('lightmap', 'cube') else [0., 0., 0., 1.])
                        for name in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube')}
            isolated[source] = base[source]
            add('isolated_' + source, pair=pair, **isolated)
        dark = dict(point=[0.] * 3, material=[0.] * 3, dir1=[0.] * 3,
                    lightmap=[0., 0., 0., .25], cube=[0., 0., 0., 1.])
        add('shared_diffuse_coefficient', pair=pair, mask=0., **dark)
        # Reflected view cosine is about .8 here. This sixth-power witness has
        # enough specular energy to reject a retained fifth-power chain despite
        # the allowed legacy _pp lobe precision.
        add('shared_specular_power', pair=pair, mask=1.,
            normal=[math.sqrt(.1), 0., math.sqrt(.9)], **dark)
        dark['dir0'] = [0.] * 3
        dark['cube'] = base['cube']
        add('shared_cube_coefficient', pair=pair, mask=1., **dark)
        # New per-profile conversion sites independently carry finite HDR,
        # exact black and negative-source sanitation through the complete PS.
        dark.update(cube=[0., 0., 0., 1.], material=[1.] * 3)
        add('shared_finite_domain', pair=pair, fp16=0,
            diffuse=[ref.CAP, 0., -1., .75], **dark)
    for gains in ([16., 1., 1.], [1., 16., 1.], [1., 1., 16.], [0., 0., 0.]):
        add('independent_gains', pair=10, gains=gains)
    for pair in (0, 10, 1, 11, 0, 2, 12, 4, 14, 2, 6, 16, 8, 18, 6):
        add('shared_vertex_alternation', pair=pair)
    # All 313 DEFAULT cases above retain their original fields and order.
    base.update(pair=20, flags=CUBE_PATTERN)
    for pair in range(20, 30):
        for depth in (0, 1):
            for reverse in (0, 1):
                add('bump_pair_depth_face', pair=pair, depth=depth, reverse=reverse)
    bump_profiles = (20, 21, 22, 23, 24, 25)
    for pair in bump_profiles:
        for label, changes in (
            ('alpha_binormal', dict(normal_sample=[.25,.5,.75,.75])),
            ('green_tangent', dict(normal_sample=[.25,.75,.75,.5])),
            ('unused_red_blue', dict(normal_sample=[1.,.5,0.,.75])),
            ('negative_q', dict(normal_sample=[.25,.9375,.75,.9375])),
            ('safe_small_q', dict(normal_sample=[.25,.5,.75,.99609375])),
            ('nonunit_basis', dict(normal=[0.,0.,.5],tangent=[2.,0.,0.],binormal=[0.,.5,0.],normal_sample=[.25,.625,.75,.75])),
            ('nonorthogonal_basis', dict(tangent=[1.,.25,.125],binormal=[.125,1.,.25],normal_sample=[.25,.75,.75,.625])),
            ('mirrored_basis', dict(binormal=[0.,-1.,0.],normal_sample=[.25,.5,.75,.75]))):
            # Cube coordinates in these normal-response cases are allowed to
            # move across texels: a constant cube isolates normal/lobe algebra.
            add('bump_'+label, pair=pair, flags=0, **changes)
        add('bump_affine',pair=pair,affine=1,flags=0,normal_sample=[.25,.625,.75,.75])
        add('bump_mask_data',pair=pair,flags=0,mask=.75)
        add('bump_opacity_data',pair=pair,flags=0,diffuse=[.5,.25,.75,.25],lightmap=[.125,.25,.0625,.75],glow=.75)
        add('bump_finite_domain',pair=pair,flags=0,fp16=0,diffuse=[ref.CAP,0.,-1.,.75],
            material=[1.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,cube=[0.,0.,0.,1.],lightmap=[0.,0.,0.,.25])
    for pair in (20,22,26):
        for lights in ((1,) if pair==26 else (0,1,8)):
            for gain in (1.,4.,16.):
                add('bump_lights_gains',pair=pair,lights=lights,gains=[gain]*3)
        for sample in ([.25,.5,.75,.5],[.25,.75,.75,.75]):
            add('bump_geometric_point',pair=pair,flags=0,normal_sample=sample,
                material=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,cube=[0.,0.,0.,1.],lightmap=[0.,0.,0.,.25])
        for reverse in (0,1):
            add('bump_fog_alpha',pair=pair,reverse=reverse,flags=FOG)
    # Desired reflection directions: every face axis plus independent offcenter
    # U/V signs. With neutral N=+Z, camera=(-R.x,-R.y,R.z) reflects to R.
    directions=((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1),
                (1,.75,-.75),(-1,-.75,.75),(.75,1,-.75),(-.75,-1,.75),(.75,-.75,1),(-.75,.75,-1))
    for i, direction in enumerate(directions):
        add('bump_cube_axis_uv',pair=bump_profiles[i%6],camera=[-direction[0],-direction[1],direction[2]],
            material=[0.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,lightmap=[0.,0.,0.,.25],mask=1.)
    # Bumped coordinates, rather than the geometric VS normal, must select a
    # different cube face; A->B and G->T produce different colored faces.
    for pair in bump_profiles:
        for sample in ([.25,.5,.75,.875],[.25,.875,.75,.5]):
            add('bump_cube_perturbed',pair=pair,normal_sample=sample,
                material=[0.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,lightmap=[0.,0.,0.,.25],mask=1.)
    # These deliberately exclude analytic RGB equivalence. Original normal ops
    # remain byte-exact by structural proof; GPU checks finite capped storage,
    # exact alpha, and unchanged temporal outputs, not an invented normal.
    for pair in (20,22,23):
        for label, changes in (
            ('q_zero',dict(normal_sample=[.25,.5,.75,1.])),
            ('near_q_positive',dict(normal_sample=[.25,.5,.75,1.-2**-23])),
            ('near_q_negative',dict(normal_sample=[.25,.5+2**-12,.75,1.])),
            ('zero_mixed_normal',dict(normal=[0.,0.,0.])),
            ('zero_view',dict(camera=[0.,0.,0.]))):
            add('bump_boundary_'+label,pair=pair,flags=BOUNDARY,fp16=0,**changes)
    for pair in (0,20,10,21,0,2,22,12,23,2,6,26,16,29,6):
        add('default_bump_alternation',pair=pair,flags=CUBE_PATTERN if pair>=20 else 0)
    return cases


def fields(c):
    return (c['gains'] + c['diffuse'] + c['lightmap'] + c['cube'] + [c['mask']] +
            c['material'] + c['point'] + c['dir0'] + c['dir1'] + c['normal'] + [c['glow']] + c['normal_sample'] + c['binormal'] +
            c['tangent'] + c['camera'] + c['fog_clip'])


def binary_cases(cases):
    data = bytearray(struct.pack('<I', len(cases)))
    for c in cases:
        data.extend(struct.pack('<9I47f', *(c[k] for k in ('id', 'pair', 'depth', 'lights', 'reverse', 'affine', 'valid', 'fp16', 'flags')), *fields(c)))
    return bytes(data)


def expected(c, half_source=False):
    if c['flags'] & BOUNDARY:
        raise ValueError('operational BUMP boundary case excludes float64 RGB equivalence')
    # Inputs are uploaded as binary32, including .6/.8 angular control values.
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    vector = lambda key: tuple(f32(x) for x in c[key])
    gains = ref.Gains(*vector('gains'))
    fixed = PAIRS[c['pair']][0] in ('badefd5143b3024f','19a246a56e9d9700')
    lights = [ref.PointLight((0, 0, 2), vector('point'), (2, .25, .125))] * (1 if fixed else c['lights'])
    varying = ref.vertex((0, 0, 0), vector('normal'), vector('camera'), vector('material'), lights,
                         fixed_single=fixed, material_alpha=.625, gains=gains,
                         fog_clip=vector('fog_clip') if c['flags'] & FOG else None)
    profile = PAIRS[c['pair']][1]
    directions = [ref.DirectionalLight((0, 0, 1), vector('dir0'))]
    if ref.PROFILES[profile].directions == 2:
        directions.append(ref.DirectionalLight((0, 0, -1), vector('dir1')))
    return ref.pixel(profile, varying, vector('diffuse'), f32(c['mask']), vector('lightmap'),
                     (cube_sample if c['flags'] & CUBE_PATTERN else lambda _:vector('cube')[:3])
                     if ref.PROFILES[profile].bump_map else vector('cube')[:3], directions, affine=AFFINE if c['affine'] else ref.IDENTITY_AFFINE,
                     face=-1 if c['reverse'] else 1, glow=f32(c['glow']), gains=gains,
                     half_source=half_source, half_target=bool(c['fp16']), normal_sample=vector('normal_sample'),
                     tangent=vector('tangent'), binormal=vector('binormal'))


def expected_alpha(c):
    alpha=.625
    if c['flags'] & FOG:
        alpha*=min(1.,max(0.,c['fog_clip'][0]-c['fog_clip'][1]*math.hypot(*c['camera'])))
    value=(c['glow']*c['lightmap'][3]+(1-c['glow'])*c['diffuse'][3])*alpha
    return ref.half(value) if c['fp16'] else value


def validate_report(text, cases=None):
    cases = fixture_cases() if cases is None else cases
    lines = text.splitlines()
    assert lines and lines[-1] == f'RESULT PASS cases={len(cases)}', 'missing final result'
    assert not any('FAIL' in line for line in lines), 'fixture reported failure'
    caps = [line for line in lines if line.startswith('CAPS ')]
    assert len(caps) == 1 and re.fullmatch(r'CAPS mrt=\d+ vs_slots=\d+ ps_slots=\d+', caps[0])
    cap = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', caps[0]))
    assert cap['mrt'] >= 3
    creates = re.findall(r'^CREATE stage=(vs|ps) key=(\S+) instructions=(\d+) words=(\d+) completed_ms=(\S+)$', text, re.M)
    assert len(creates) == len([l for l in lines if l.startswith('CREATE ')]) and creates
    assert len({(r[0], r[1]) for r in creates}) == len(creates), 'duplicate shader creation'
    required = {(stage, shader, str(depth)) for vs, ps in PAIRS for stage, shader in [('vs', vs), ('ps', ps)] for depth in (0, 1)}
    observed = set()
    for stage, key, count, words, ms in creates:
        shader, mode, depth, *_ = key.split('_')
        assert 0 < int(count) <= cap[stage + '_slots'] and int(words) > int(count)
        assert math.isfinite(float(ms)) and float(ms) >= 0
        if mode == '2': observed.add((stage, shader, depth))
    assert required <= observed, 'missing combined program/depth creation'
    invariants = re.findall(r'^INVARIANT id=(\d+) pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0 rgb_bad=0$', text, re.M)
    assert list(map(int, invariants)) == list(range(len(cases))), 'missing or failed invariant'
    samples = re.findall(r'^SAMPLE id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$', text, re.M)
    assert len(samples) == len(cases) * 9, 'missing numerical sample'
    seen, maximum_abs, maximum_scaled, black_samples, hdr_samples = set(), 0., 0., 0, 0
    failures = []
    family_results = {name:dict(cases=sum(family_name(c['pair']) == name for c in cases),
                               max_rgb_envelope_error=0., max_tolerance_fraction=0.)
                      for name in ('Argon', 'shared DEFAULT', 'Argon BUMP')}
    for cid, x, y, rgba in samples:
        cid, x, y = int(cid), int(x), int(y)
        assert 0 <= cid < len(cases) and x in (4, 8, 12) and y in (4, 8, 12)
        assert (cid, x, y) not in seen, 'duplicate sample'
        seen.add((cid, x, y))
        c = cases[cid]
        family = family_results[family_name(c['pair'])]
        actual = tuple(map(float, rgba.split(',')))
        assert len(actual) == 4 and all(map(math.isfinite, actual)), (cid, 'nonfinite GPU output')
        assert actual[3] == expected_alpha(c), (cid, 'authored alpha')
        if c['flags'] & BOUNDARY:
            ceiling=ref.half(ref.encode(ref.CAP)) if c['fp16'] else ref.encode(ref.CAP)*(1+1e-5)
            assert all(0 <= v <= ceiling for v in actual[:3]), (cid, 'boundary storage outside finite encoded cap')
            continue
        ideal, quantized = expected(c), expected(c, half_source=True)
        for k, value in enumerate(actual[:3]):
            # Retained _pp samples may keep binary32 or narrow to binary16;
            # use the envelope, plus bounded legacy lobe/FP32 transfer error.
            lo = min(ideal.encoded_rgba[k], quantized.encoded_rgba[k])
            hi = max(ideal.encoded_rgba[k], quantized.encoded_rgba[k])
            error = max(lo - value, value - hi, 0.)
            absolute = 1e-12 if c['label'].endswith('_tiny') else RGB_ABS_TOL
            tolerance = absolute + RGB_REL_TOL * max(abs(lo), abs(hi))
            maximum_abs = max(maximum_abs, error)
            maximum_scaled = max(maximum_scaled, error / tolerance)
            family['max_rgb_envelope_error'] = max(family['max_rgb_envelope_error'], error)
            family['max_tolerance_fraction'] = max(family['max_tolerance_fraction'], error / tolerance)
            if error > tolerance: failures.append((cid, c['label'], k, value, lo, hi, tolerance))
            if not c['fp16'] and lo > 0:
                assert value > 0, (cid, 'positive full-range path collapsed to zero')
            if lo == hi == 0:
                assert value == 0 and math.copysign(1., value) == 1., (cid, 'black not exact +0')
                black_samples += 1
            if lo > 1: hdr_samples += 1
        # All fixture alpha inputs are exactly representable and the independent
        # whole-RT motion-only comparison also checks bit-for-bit alpha equality.
        assert actual[3] == ideal.encoded_rgba[3], (cid, 'authored alpha')
    assert not failures, ('RGB oracle mismatches', failures[:12], 'total', len(failures))
    timings = re.findall(r'^TIMING pair=(0|10|20) lights=(0|8) mode=([012]) iteration=(\d+) draws=4 vertices=98304 width=256 completed_ms=(\S+)$', text, re.M)
    assert len(timings) == 36 * len(TIMING_PAIRS)
    timing_summary = []
    for pair in TIMING_PAIRS:
        for lights in (0, 8):
            rows = [r for r in timings if int(r[0]) == pair and int(r[1]) == lights]
            assert [int(r[3]) for r in rows] == list(range(18))
            assert [int(r[2]) for r in rows] == [2-i%3 if (i//3)%2 else i%3 for i in range(18)]
            for mode in range(3):
                values = [float(r[4]) for r in rows if int(r[2]) == mode]
                assert len(values) == 6 and all(math.isfinite(v) and v >= 0 for v in values)
                timing_summary.append(dict(pair=pair, family=family_name(pair),
                                           lights=lights, mode=('original', 'motion', 'combined')[mode],
                                           samples=6, median_ms=statistics.median(values), min_ms=min(values), max_ms=max(values)))
    recognized = 1 + len(creates) + len(invariants) + len(samples) + len(timings) + 1
    assert len(lines) == recognized, 'unexpected output rows'
    return dict(cases=len(cases), pairs=len(PAIRS), unique_originals=24, shader_creations=len(creates),
                samples=len(samples), analytic_samples=9*sum(not bool(c["flags"] & BOUNDARY) for c in cases), families=family_results, original_case_prefix=313,
                boundary_cases=sum(bool(c["flags"] & BOUNDARY) for c in cases),
                boundary_limit="Finite capped RGB storage and alpha/temporal identity only; no float64 full-color equivalence", invariant_pixels=256*len(cases), max_rgb_envelope_error=maximum_abs,
                max_tolerance_fraction=maximum_scaled, exact_black_channels=black_samples,
                hdr_channels=hdr_samples, caps=cap, timings=timing_summary,
                max_executable_instructions={stage:max(int(r[2]) for r in creates if r[0]==stage) for stage in ('vs','ps')},
                create_total_ms=sum(float(r[4]) for r in creates))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=EXE)
    parser.add_argument('--programs', type=Path, default=PROGRAMS)
    parser.add_argument('--raw-dir', type=Path, default=Path('/tmp/x3-linear-material-gpu'))
    args = parser.parse_args()
    assert bottle.BOTTLE == 'X3', 'new fixtures require X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(), 'game running; fixture refused'
    cases = fixture_cases()
    args.raw_dir.mkdir(parents=True, exist_ok=True)
    case_file = args.raw_dir/'cases.bin'
    case_file.write_bytes(binary_cases(cases))
    report = args.raw_dir/'report.txt'
    result_path = bottle.results_dir(ROOT)/'linear-material-gpu.json'
    inputs = {f'{stage}_{shader}.bin': sha(args.programs/f'{stage}_{shader}.bin')
              for vs, ps in PAIRS for stage, shader in [('vs', vs), ('ps', ps)]}
    profiles = json.loads((ROOT/'docs/reverse-engineering/linear-material-profiles.json').read_text())
    assert inputs == {p['id']+'.bin':p['sha256'] for p in profiles['programs']}, 'original input provenance'
    result = dict(passed=False, bottle=bottle.describe(), game_launched=False,
                  render_contract=dict(sampler_indices=[0,1,2,3,4],sampler_srgb=False,srgb_write=False,msaa=False,targets=['RGBA16F/RGBA32F','RGBA32F','R32F']),
                  scope='Argon plus shared Khaak/Teladi/Teladi_nodiff/Xenon DEFAULT plus Argon BUMPMAP; detached combined shader numerics, alpha/motion identity and diagnostic cost; no live route or native Windows runtime proof',
                  timing_scope='QPC through EVENT completion; 4 managed-buffer DrawPrimitive calls, 98,304 vertices, one Begin/EndScene, fenced setup, no readback; not GPU timestamps or game FPS',
                  tolerance=dict(rgb_relative=RGB_REL_TOL,rgb_absolute=RGB_ABS_TOL,tiny_rgb_absolute=1e-12,retained_sample_precision='float32/binary16 reference envelope',alpha='exact'),
                  original_sha256=inputs, executable_sha256=sha(args.exe), raw_report=str(report),
                  code_sha256={name:sha(ROOT/name) for name in CODE_INPUTS})
    wine=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
    command=[str(wine),'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b',str(args.exe),'Z:'+str(args.programs),'Z:'+str(case_file)]
    try:
        with report.open('w') as out,(args.raw_dir/'wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=1200)
        result['exit_code']=process.returncode
        assert process.returncode==0, 'fixture failed; see '+str(report)
        result.update(validate_report(report.read_text(),cases))
        assert sha(args.exe)==result['executable_sha256'], 'executable changed'
        assert result['code_sha256']=={name:sha(ROOT/name) for name in CODE_INPUTS}, 'fixture/core/reference changed during run'
        assert all(sha(args.programs/name)==value for name,value in inputs.items()), 'original changed'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        # Retain the checked-in accepted result when this run fails. Failure
        # diagnostics belong beside its local raw output until resolved.
        destination=result_path if result['passed'] else args.raw_dir/'failed-result.json'
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k in ('passed','cases','samples','error','max_tolerance_fraction')}))


if __name__ == '__main__':
    main()
