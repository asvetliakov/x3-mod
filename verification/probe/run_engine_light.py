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
- cost: the per-draw CPU path (lookup, constants, SetPixelShaderConstantF of three registers) <= 0.2 us on a hit; the
  GPU cost of the term over a full-screen hull at 1920x1080 and 5120x1440 (median of five EVENT-fenced batches).

Run as  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_engine_light.py
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


def expected_constants(case, tints, k):
    value = case['value']
    light = [case['nozzle'][i] + case['axis'][i] * k['behind'] * value for i in range(3)]
    rel = [light[i] - case['camera'][i] for i in range(3)]
    intensity = k['core_low'] + (k['core_high'] - k['core_low']) * min(max(case['s'], 0.0), 1.0)
    colour = [tints[case['cluster']][i] * intensity * k['preset'][case['preset']] * k['colour_scale'] for i in range(3)]
    radius = k['reach'] * value
    return rel + [radius * radius] + colour + [1.0 / (radius * radius)] + [0.0, 0.0, 1.0, 0.0]


def parse(text):
    cases, invariants, samples = {}, {}, {}
    out = dict(programs=[], create=None, reset=None, cost=None, gpu=[], done='DONE' in text.split(),
               teardown=[line for line in text.splitlines() if line.startswith('TEARDOWN ')])
    for line in text.splitlines():
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
        elif line.startswith('GPU '):
            out['gpu'].append(dict(width=int(fields['width']), height=int(fields['height']),
                                   base_ms=[float(v) for v in fields['base_ms'].split(',')],
                                   twin_ms=[float(v) for v in fields['twin_ms'].split(',')]))
    out.update(cases=cases, invariants=invariants, samples=samples)
    return out


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
    cost = parsed['cost']
    result['cost_checks'] = dict(hit_ns=cost['hit_ns'] <= HIT_NS_LIMIT, hits=cost['hits'] == cost['rounds'], misses=cost['misses'] == cost['rounds'])
    result['gpu'] = []
    for g in parsed['gpu']:
        base, twin = statistics.median(g['base_ms']), statistics.median(g['twin_ms'])
        result['gpu'].append(dict(width=g['width'], height=g['height'], base_ms=base, twin_ms=twin, term_us=(twin - base) * 1e3,
                                  base_batches=g['base_ms'], twin_batches=g['twin_ms']))
    result['reset_ok'] = parsed['reset'] is not None and parsed['reset']['differ'] == 0
    result['passed'] = (all(c['passed'] for c in result['cases'].values()) and all(result['cost_checks'].values()) and
                        result['reset_ok'] and len(result['gpu']) == 2 and len(result['cases']) >= 9)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=float, default=420)
    args = parser.parse_args()
    if bottle.BOTTLE != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    warnings = None if args.no_build else build()
    report_path = BUILD / 'engine-light-report.txt'
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', str(EXE), 'Z:' + str(PROGRAMS)]
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
    record = dict(passed=False, bottle=bottle.describe(), game_launched=False, exit_code=exit_code, timeout=timeout,
                  elapsed_s=round(elapsed, 2), build_warnings=warnings, executable_sha256=sha(EXE),
                  sources_sha256={s: sha(ROOT / s) for s in SOURCES}, raw_report=str(report_path.relative_to(ROOT)))
    try:
        assert exit_code == 0, 'fixture exit %s, timeout %s (see %s)' % (exit_code, timeout, report_path)
        record.update(validate(report))
    except BaseException as error:
        record['error'] = repr(error)
    path = bottle.results_dir(ROOT) / 'engine-light-gpu.json'
    path.write_text(json.dumps(record, indent=1, sort_keys=True) + '\n')
    summary = dict(passed=record['passed'], error=record.get('error'), cost=record.get('cost'), gpu=[{k: g[k] for k in ('width', 'height', 'base_ms', 'twin_ms', 'term_us')} for g in record.get('gpu', [])],
                   cases={cid: dict(passed=c['passed'], max_relative=round(c['stats']['max_relative'], 6), lit=c['stats']['lit'], zero=c['stats']['zero'],
                                    capped=c['stats']['capped'], depth=c['stats']['max_depth_error'], checks=[n for n, v in c['checks'].items() if not v])
                          for cid, c in record.get('cases', {}).items()},
                   reset=record.get('reset'), create=record.get('create'), record=str(path.relative_to(ROOT)))
    print(json.dumps(summary, indent=1))
    raise SystemExit(0 if record['passed'] else 1)


if __name__ == '__main__':
    main()
