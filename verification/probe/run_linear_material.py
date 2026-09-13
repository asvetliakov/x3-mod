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
                dir0=[.375, .25, .5], dir1=[.125, .5, .25], normal=[0., 0., 1.], glow=.25)

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
    return cases


def fields(c):
    return (c['gains'] + c['diffuse'] + c['lightmap'] + c['cube'] + [c['mask']] +
            c['material'] + c['point'] + c['dir0'] + c['dir1'] + c['normal'] + [c['glow']])


def binary_cases(cases):
    data = bytearray(struct.pack('<I', len(cases)))
    for c in cases:
        data.extend(struct.pack('<8I32f', *(c[k] for k in ('id', 'pair', 'depth', 'lights', 'reverse', 'affine', 'valid', 'fp16')), *fields(c)))
    return bytes(data)


def expected(c, half_source=False):
    # Inputs are uploaded as binary32, including .6/.8 angular control values.
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    vector = lambda key: tuple(f32(x) for x in c[key])
    gains = ref.Gains(*vector('gains'))
    fixed = c['pair'] >= 6
    lights = [ref.PointLight((0, 0, 2), vector('point'), (2, .25, .125))] * (1 if fixed else c['lights'])
    varying = ref.vertex((0, 0, 0), vector('normal'), (0, 0, 4), vector('material'), lights,
                         fixed_single=fixed, material_alpha=.625, gains=gains)
    profile = PAIRS[c['pair']][1]
    directions = [ref.DirectionalLight((0, 0, 1), vector('dir0'))]
    if ref.PROFILES[profile].directions == 2:
        directions.append(ref.DirectionalLight((0, 0, -1), vector('dir1')))
    return ref.pixel(profile, varying, vector('diffuse'), f32(c['mask']), vector('lightmap'),
                     vector('cube')[:3], directions, affine=AFFINE if c['affine'] else ref.IDENTITY_AFFINE,
                     face=-1 if c['reverse'] else 1, glow=f32(c['glow']), gains=gains,
                     half_source=half_source, half_target=bool(c['fp16']))


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
    invariants = re.findall(r'^INVARIANT id=(\d+) pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0$', text, re.M)
    assert list(map(int, invariants)) == list(range(len(cases))), 'missing or failed invariant'
    samples = re.findall(r'^SAMPLE id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$', text, re.M)
    assert len(samples) == len(cases) * 9, 'missing numerical sample'
    seen, maximum_abs, maximum_scaled, black_samples, hdr_samples = set(), 0., 0., 0, 0
    failures = []
    for cid, x, y, rgba in samples:
        cid, x, y = int(cid), int(x), int(y)
        assert 0 <= cid < len(cases) and x in (4, 8, 12) and y in (4, 8, 12)
        assert (cid, x, y) not in seen, 'duplicate sample'
        seen.add((cid, x, y))
        c = cases[cid]
        actual = tuple(map(float, rgba.split(',')))
        assert len(actual) == 4 and all(map(math.isfinite, actual)), (cid, 'nonfinite GPU output')
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
    timings = re.findall(r'^TIMING lights=(0|8) mode=([012]) iteration=(\d+) draws=4 vertices=98304 width=256 completed_ms=(\S+)$', text, re.M)
    assert len(timings) == 36
    timing_summary = []
    for lights in (0, 8):
        rows = [r for r in timings if int(r[0]) == lights]
        assert [int(r[2]) for r in rows] == list(range(18))
        assert [int(r[1]) for r in rows] == [2-i%3 if (i//3)%2 else i%3 for i in range(18)]
        for mode in range(3):
            values = [float(r[3]) for r in rows if int(r[1]) == mode]
            assert len(values) == 6 and all(math.isfinite(v) and v >= 0 for v in values)
            timing_summary.append(dict(lights=lights, mode=('original', 'motion', 'combined')[mode],
                                       samples=6, median_ms=statistics.median(values), min_ms=min(values), max_ms=max(values)))
    recognized = 1 + len(creates) + len(invariants) + len(samples) + len(timings) + 1
    assert len(lines) == recognized, 'unexpected output rows'
    return dict(cases=len(cases), pairs=10, unique_originals=9, shader_creations=len(creates),
                samples=len(samples), invariant_pixels=256*len(cases), max_rgb_envelope_error=maximum_abs,
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
                  render_contract=dict(sampler_srgb=False,srgb_write=False,msaa=False,targets=['RGBA16F/RGBA32F','RGBA32F','R32F']),
                  scope='Detached combined shader numerics, alpha/motion identity and diagnostic cost; no live route or native Windows runtime proof',
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
        result_path.parent.mkdir(parents=True,exist_ok=True)
        result_path.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k in ('passed','cases','samples','error','max_tolerance_fraction')}))


if __name__ == '__main__':
    main()
