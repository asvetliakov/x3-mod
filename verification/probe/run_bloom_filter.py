#!/usr/bin/env python3
"""Standalone GPU bloom differential fixture; never starts the game/proxy.

Run through wine_lock.py. --build-only only cross-compiles and prepares cases
under a fresh temporary directory; it never executes Wine or publishes a GPU
pass result. Raw binaries/readbacks remain there, with hashed summary pointers.
"""
from __future__ import annotations

import argparse
import dataclasses
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile

import bottle
import bloom_characterization as characterization
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bloom_reference as ref

GENERATOR = ROOT / 'tools/shaders/generate_rigid_motion_pixel.py'
spec = importlib.util.spec_from_file_location('bloom_fixture_shader_generator', GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

# Fixed before GPU execution. FP16 stores round within roughly 0.05%; these
# limits additionally cover full-precision SM3 arithmetic, transcendental
# approximation and hardware bilinear interpolation. Assess every stored stage
# to prevent hidden accumulation. Never widen these merely to fit a failure.
ABS_TOLERANCE = 0.002
REL_TOLERANCE = 0.003
GENERATIONS = 2
KERNELS = ('extract_gamma', 'extract_srgb', 'extract_none', 'extract_even_gamma',
           'extract_even_srgb', 'extract_even_none', 'down', 'up')
BUILD_FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2',
               '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-static']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def half(v):
    return struct.unpack('<e', struct.pack('<e', v))[0]


def quantize(image, alpha=False):
    return [[tuple(half(c) for c in p[:3]) + ((half(p[3]),) if alpha else ())
             for p in row] for row in image]


def make_cases():
    cases = []
    def add(name, image, params=ref.Params(), exposure=1., clamp=0., mode='none', group='numerical'):
        params = dataclasses.replace(params, **{key: f32(getattr(params, key))
                                    for key in ('strength', 'threshold', 'knee', 'scatter')})
        image = [[tuple(p[:3]) + (p[3] if len(p) == 4 else .375,) for p in row] for row in image]
        cases.append(dict(name=name, image=quantize(image, alpha=True), params=params,
                          exposure=f32(exposure), clamp=f32(clamp), mode=mode, group=group))
    def solid(w, h, rgb):
        return [[rgb for _ in range(w)] for _ in range(h)]
    # Named sampling/FP16 controls: exact points/DC plus nonconstant odd area.
    add('sampling_1px_hdr', solid(1, 1, (16., 4., .5)), ref.Params(threshold=0), group='sampling')
    add('sampling_odd_ramp', [[(x + y * 2., x * .5, y * .25) for x in range(7)]
                            for y in range(5)], ref.Params(threshold=0), group='sampling')
    for mode in ref.DECODE_MODES:
        add('black_' + mode, solid(7, 3, (0., 0., 0.)), mode=mode)
        add('dc_' + mode, solid(17, 9, (8., 4., 2.)), mode=mode)
        add('dc_even_' + mode, solid(8, 6, (8., 4., 2.)), mode=mode)
        add('maximum_' + mode, solid(5, 3, (65504.,) * 3), ref.Params(threshold=0),
            exposure=65504., mode=mode, group='finite_limit')
    for levels in (1, 2, 6):
        for scatter in (0., 1.):
            add(f'dc_levels{levels}_scatter{scatter}', solid(17, 9, (8., 4., 2.)),
                ref.Params(levels=levels, scatter=scatter))
    for w, h in ((17, 13), (1, 17), (17, 1), (1, 1), (8, 6)):
        image = solid(w, h, (0., 0., 0.))
        image[h // 2][w // 2] = (8., 4., 2.)
        add(f'impulse_{w}x{h}', image, ref.Params(threshold=0))
    for width, position in ((82, 1), (8462, 8459), (15611, 15599)):
        image = solid(width, 1, (0., 0., 0.))
        image[0][position] = (65504., 32752., 16376.)
        add(f'geometry_{width}', image, ref.Params(threshold=0), group='integer_geometry')
    image = [[(v,) * 3 for v in (0., .5, 1., 1.5, 2., 2.5, 3., 4., 8.)]]
    for knee in (0., .5, 1.):
        add(f'threshold_knee{knee}', image, ref.Params(threshold=2, knee=knee))
    add('decode_before_average', [[(0.,) * 3, (2.,) * 3]],
        ref.Params(threshold=1, knee=0), mode='gamma2.2')
    add('exposure_threshold', image, ref.Params(threshold=2), exposure=2.)
    add('clamp_before_exposure', image, ref.Params(threshold=0), exposure=4., clamp=2.)
    add('gamma_clamp_before_exposure', image, ref.Params(threshold=0), exposure=.5, clamp=2., mode='gamma2.2')
    add('srgb_breakpoint', [[(x,) * 3 for x in (0., .02, .04045, .041, .1, .5, 1., 2., 4.)]],
        ref.Params(threshold=0), mode='srgb')
    rng = random.Random(93014)
    add('random_odd_chroma', [[tuple(rng.uniform(0., 8.) for _ in range(3)) for _ in range(13)]
                             for _ in range(7)])
    # These exercise only bloom extraction's documented sanitizer. No claim
    # about arbitrary NaNs in the base AgX path or filtering poisoned scratch.
    for mode in ref.DECODE_MODES:
        add('nonfinite_extract_' + mode,
            [[(math.nan, math.inf, -math.inf), (-1., -0., 1.), (2., 4., 8.)]],
            ref.Params(threshold=0), mode=mode, group='nonfinite_extraction')
    return cases


def case_metadata(case):
    return {k: (dataclasses.asdict(v) if k == 'params' else v)
            for k, v in case.items() if k != 'image'} | {
        'width': len(case['image'][0]), 'height': len(case['image'])}


def write_bundle(cases, path):
    with path.open('wb') as file:
        file.write(b'X3BLM001' + struct.pack('<I', len(cases)))
        for case in cases:
            p = case['params']
            file.write(struct.pack('<4I6f', len(case['image'][0]), len(case['image']), p.levels,
                ref.DECODE_MODES.index(case['mode']), p.strength, p.threshold, p.knee, p.scatter,
                case['exposure'], case['clamp']))
            for row in case['image']:
                for pixel in row:
                    file.write(struct.pack('<4e', *pixel))


def reference_stages(case):
    """Independent nine-tap oracle, quantized at upload and EVERY FP16 store."""
    image = quantize(case['image'], alpha=True)
    p = case['params']
    current = [[ref.prefilter(pixel, p, case['exposure'], case['clamp'], case['mode'])
                for pixel in row] for row in image]
    down, stages = [], {}
    for i, _ in enumerate(ref.layout(len(image[0]), len(image), p.levels)):
        current = quantize(ref.downsample(current))
        down.append(current)
        stages[f'd{i}'] = current
    for i in range(len(down) - 2, -1, -1):
        fine = down[i]
        coarse = ref.tent(current, len(fine[0]), len(fine))
        current = quantize([[tuple(min((1 - p.scatter) * a + p.scatter * b, ref.FP16_MAX)
                                     for a, b in zip(pixel, other))
                             for pixel, other in zip(row, other_row)]
                            for row, other_row in zip(fine, coarse)])
        stages[f'u{i}'] = current
    stages['final'] = quantize(ref.tent(current, len(image[0]), len(image)))
    return stages


def compare_image(path, expected, require_exact_black=False):
    height, width = len(expected), len(expected[0])
    data = path.read_bytes()
    if len(data) != width * height * 8:
        raise ValueError('readback byte count mismatch')
    errors, max_error, max_ratio, channels = [], 0., 0., 0
    for index, actual in enumerate(struct.iter_unpack('<4e', data)):
        wanted = expected[index // width][index % width]
        if actual[3] != 0:
            errors.append(dict(pixel=index, channel=3, reason='bloom alpha must be zero'))
        for channel in range(3):
            value, target = actual[channel], wanted[channel]
            tolerance = ABS_TOLERANCE + REL_TOLERANCE * abs(target)
            channels += 1
            if not math.isfinite(value) or value < 0 or value > ref.FP16_MAX:
                errors.append(dict(pixel=index, channel=channel, reason='nonfinite/out-of-bounds result'))
                continue
            if require_exact_black and value != 0:
                errors.append(dict(pixel=index, channel=channel, reason='black scene must remain exactly black'))
            error = abs(value - target)
            max_error = max(max_error, error)
            max_ratio = max(max_ratio, error / tolerance)
            if error > tolerance:
                errors.append(dict(pixel=index, channel=channel, actual=value, expected=target,
                                   absolute_error=error, tolerance=tolerance))
    return dict(passed=not errors, channels=channels, max_absolute_error=max_error,
                max_tolerance_ratio=max_ratio, failure_count=len(errors), failures=errors[:8],
                file=str(path), sha256=digest(path))


def require_runner_lock():
    """Refuse accidental direct Wine execution outside wine_lock.py."""
    with open('/tmp/x3-wine-runner.lock', 'a+') as file:
        try:
            fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            pass
        else:
            fcntl.flock(file, fcntl.LOCK_UN)
            raise RuntimeError('Run this command through verification/probe/wine_lock.py')
        file.seek(0)
        owner = file.read(1024).split()[0]
        current = os.getpid()
        for _ in range(16):
            if str(current) == owner:
                return
            result = subprocess.run(['ps', '-o', 'ppid=', '-p', str(current)],
                                    capture_output=True, text=True, check=True, timeout=5)
            current = int(result.stdout.strip())
            if current <= 1:
                break
        raise RuntimeError('Wine lock holder is not a parent of this runner')


def host_module_path(windows_path):
    normalized = windows_path.replace('\\', '/')
    if normalized[:2].lower() == 'z:':
        return Path(normalized[2:]).resolve()
    if normalized[:2].lower() == 'c:':
        return (bottle.bottle_dir() / 'drive_c' / normalized[3:]).resolve()
    raise ValueError('Unmapped native module path: ' + windows_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--characterize-only', action='store_true',
                        help='Run independent sampler/store/UV controls; do not evaluate bloom')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    retained = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='x3-bloom-gpu-'))
    retained.mkdir(parents=True, exist_ok=True)
    if any(retained.iterdir()):
        raise RuntimeError('Output directory must be empty to preserve prior evidence')
    sources = {Path(__file__).resolve(), ROOT / 'verification/probe/bloom_filter_fixture.cpp',
        ROOT / 'src/temporal/bloom.h', ROOT / 'src/temporal/agx.h',
        ROOT / 'tools/analysis/bloom_reference.py', ROOT / 'tools/analysis/agx_reference.py',
        ROOT / 'verification/probe/bottle.py', ROOT / 'verification/probe/game_guard.py',
        ROOT / 'verification/probe/wine_lock.py', GENERATOR}
    sources.add(ROOT / 'verification/probe/bloom_characterization.py')
    expanded = []
    shader_names = ('quad_vs', *(f'bloom_{name}_ps' for name in KERNELS))
    for name in shader_names:
        path = ROOT / f'src/temporal/{name}.hlsl'
        _, includes = generator.expand_includes(path)
        sources.update([path, *includes])
    before = {str(p): digest(p) for p in sorted(sources)}
    for name in shader_names:
        path = ROOT / f'src/temporal/{name}.hlsl'
        text, _ = generator.expand_includes(path)
        output = retained / (name + '.hlsl')
        output.write_text(text)
        expanded.append(output)
    exe = retained / 'bloom_filter_fixture.exe'
    build = ['i686-w64-mingw32-g++', *BUILD_FLAGS, str(ROOT / 'verification/probe/bloom_filter_fixture.cpp'),
             '-o', str(exe), '-luser32']
    subprocess.run(build, cwd=ROOT, check=True)
    cases = make_cases()
    bundle = retained / 'cases.bin'
    write_bundle(cases, bundle)
    characterization_bundle = retained / 'characterization.bin'
    characterization.write_inputs(characterization_bundle)
    report = dict(schema=1, passed=False, phase='built', scope='standalone bloom GPU numerics only',
        native_windows_runtime_verified=False, game_launched=False, installed_dll_changed=False,
        renderer_integration_verified=False, caller_state_restoration_verified=False,
        gpu_execution_verified=False, retained=str(retained), bottle=bottle.describe(),
        sources_before=before, build_command=build, executable_sha256=digest(exe),
        case_bundle_sha256=digest(bundle), cases=[case_metadata(c) for c in cases],
        characterization_bundle_sha256=digest(characterization_bundle),
        characterization_only=args.characterize_only,
        expanded_sources={str(p): digest(p) for p in expanded},
        precision=dict(input='FP16', stores='FP16 each pass', cpu_arithmetic='double',
                       absolute_tolerance=ABS_TOLERANCE, relative_tolerance=REL_TOLERANCE,
                       formula='absolute + relative * abs(expected)'), images=[])
    report['sources_after_build'] = {p: digest(p) for p in before}
    if report['sources_after_build'] != before:
        raise RuntimeError('Sources changed during shader expansion/cross-compilation')
    local_summary = retained / 'summary.json'
    def write_report():
        local_summary.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    write_report()
    if args.build_only:
        report['sources_after'] = {p: digest(p) for p in before}
        if report['sources_after'] != before:
            raise RuntimeError('Sources changed during cross-compilation')
        write_report()
        print(json.dumps(dict(build_only=True, gpu_execution_verified=False,
                              cases=len(cases), summary=str(local_summary))))
        return
    require_runner_lock()
    if game_running():
        raise RuntimeError('X3AP is running; postpone the GPU fixture')
    compiler = bottle.game_dir() / 'd3dx9_37.dll'
    report['compiler_sha256_before'] = digest(compiler)
    runtime_candidates = [bottle.bottle_dir() / 'drive_c/windows/system32' / name
                          for name in ('d3d9.dll', 'wined3d.dll')]
    report['runtime_files_before'] = {str(p.resolve()): digest(p) for p in runtime_candidates if p.exists()}
    windows = lambda p: 'Z:' + str(p)
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(retained),
               str(exe), windows(compiler), *map(windows, expanded), windows(bundle), windows(retained)]
    if args.characterize_only:
        command.append('characterize-only')
    report.update(command=command, phase='running')
    write_report()
    summary = bottle.results_dir(ROOT) / ('bloom-filter-characterization-summary.json' if args.characterize_only
                                          else 'bloom-filter-summary.json')
    if summary.exists():
        (retained / 'prior-summary.json').write_bytes(summary.read_bytes())
    try:
        stdout, stderr = retained / 'fixture.txt', retained / 'wine.log'
        with stdout.open('w') as out, stderr.open('w') as err:
            result = subprocess.run(command, stdout=out, stderr=err, timeout=180,
                                    env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        report.update(exit_code=result.returncode, stdout_sha256=digest(stdout), stderr_sha256=digest(stderr))
        text = stdout.read_text()
        modules = {}
        for name, path in re.findall(r'^MODULE name=(\S+) path=(.+)$', text, re.MULTILINE):
            host_path = host_module_path(path.strip())
            modules[name] = dict(native_path=path.strip(), host_path=str(host_path), sha256=digest(host_path))
        report['loaded_modules'] = modules
        if modules.get('d3dx', {}).get('sha256') != report['compiler_sha256_before']:
            raise RuntimeError('Loaded compiler does not match the recorded requested compiler')
        report['caps'] = re.findall(r'^CAPS .+$', text, re.MULTILINE)
        report['format_checks'] = re.findall(r'^FORMAT .+$', text, re.MULTILINE)
        caps_match = re.search(r'^CAPS ps=(\S+) vs=(\S+) max_width=(\d+) max_height=(\d+)$', text, re.MULTILINE)
        if not caps_match:
            raise RuntimeError('Missing device dimensions')
        max_width, max_height = int(caps_match[3]), int(caps_match[4])
        if args.characterize_only:
            match = re.search(r'^RESULT CHARACTERIZATION PASS controls=(\d+)$', text, re.MULTILINE)
            if result.returncode or not match or 'FAIL' in text:
                raise RuntimeError('Independent characterization GPU pass did not complete')
            report['characterization'] = characterization.analyze(retained, cases, max_width, max_height)
            report['characterization']['controls_executed'] = int(match[1])
            report['characterization_artifacts'] = {p.name: digest(p) for p in retained.glob('characterization_*.rgba*')}
            report['characterization_programs'] = {p.name: digest(p) for p in retained.glob('char_*.cso')}
            report['characterization_shader_sources'] = {p.name: digest(p) for p in retained.glob('char_*.hlsl')}
            report['compiled_shaders'] = {name: digest(retained / (name + '.cso')) for name in ('quad', *KERNELS)}
            if not report['characterization']['passed']:
                raise RuntimeError(report['characterization'].get('error', 'Characterization failed'))
            report.update(passed=True, phase='characterized', characterization_verified=True,
                          gpu_execution_verified=False)
            return
        skipped = set()
        report['skipped_cases'] = []
        for entry in re.finditer(r'^SKIP case=(\d+) reason=dimension_caps width=(\d+) height=(\d+)$', text, re.MULTILINE):
            index, width, height = map(int, entry.groups())
            if index not in range(len(cases)) or index in skipped:
                raise RuntimeError('Invalid/duplicate skipped case')
            case = cases[index]
            if (width, height) != (len(case['image'][0]), len(case['image'])) \
                or (width <= max_width and height <= max_height) or case['group'] != 'integer_geometry':
                raise RuntimeError('Unsupported or unjustified case skip')
            skipped.add(index)
            report['skipped_cases'].append(dict(case=index, name=case['name'], reason='dimension_caps',
                                                width=width, height=height))
        match = re.search(r'^RESULT PASS cases=(\d+) generations=(\d+) passes=(\d+)$', text, re.MULTILINE)
        expected_stages = {i: reference_stages(case) for i, case in enumerate(cases) if i not in skipped}
        expected_count = GENERATIONS * sum(len(stages) for stages in expected_stages.values())
        if result.returncode or not match or tuple(map(int, match.groups())) != (len(cases) - len(skipped), GENERATIONS, expected_count):
            raise RuntimeError('GPU fixture did not complete every expected pass')
        if 'RESET PASS' not in text or 'FAIL' in text:
            raise RuntimeError('GPU fixture Reset/API failure')
        case_records = {}
        for entry in re.finditer(r'^CASE generation=(\d+) case=(\d+) extraction=(\S+) levels=(\d+)$', text, re.MULTILINE):
            generation, index, extraction, levels = entry.groups()
            generation, index, levels = int(generation), int(index), int(levels)
            if (generation, index) in case_records or generation not in range(GENERATIONS) or index not in expected_stages:
                raise RuntimeError('Invalid/duplicate case record')
            case = cases[index]
            w, h = len(case['image'][0]), len(case['image'])
            variant = 'extract_' + ('even_' if w % 2 == 0 and h % 2 == 0 else '') \
                + ('gamma' if case['mode'] == 'gamma2.2' else case['mode'])
            if extraction != variant or levels != len(ref.layout(w, h, case['params'].levels)):
                raise RuntimeError('Incorrect extraction variant/level count')
            case_records[generation, index] = extraction
        if len(case_records) != (len(cases) - len(skipped)) * GENERATIONS:
            raise RuntimeError('Missing case records')
        report['extraction_variants_executed'] = sorted(set(case_records.values()))
        if set(report['extraction_variants_executed']) != set(KERNELS[:6]):
            raise RuntimeError('Not all six extraction programs executed')
        seen = set()
        for entry in re.finditer(r'^IMAGE generation=(\d+) case=(\d+) stage=(\S+) width=(\d+) height=(\d+) file=(\S+)$', text, re.MULTILINE):
            generation, index, stage, width, height, filename = entry.groups()
            generation, index, width, height = map(int, (generation, index, width, height))
            key = (generation, index, stage)
            if key in seen or generation not in range(GENERATIONS) or index not in expected_stages:
                raise RuntimeError('Unexpected/duplicate image record')
            seen.add(key)
            expected = expected_stages[index][stage]
            if (width, height) != (len(expected[0]), len(expected)) or filename != f'g{generation}_c{index}_{stage}.rgba16f':
                raise RuntimeError('Readback metadata mismatch')
            comparison = compare_image(retained / filename, expected, cases[index]['name'].startswith('black_'))
            report['images'].append(dict(generation=generation, case=index, name=cases[index]['name'],
                                          stage=stage, **comparison))
        if len(seen) != expected_count:
            raise RuntimeError('Missing image records')
        report['sampling_selftests'] = [dict(name=case['name'], passed=all(
            image['passed'] for image in report['images'] if image['case'] == index))
            for index, case in enumerate(cases) if case['group'] == 'sampling']
        report['gpu_admitted_dimensions'] = dict(max_width=max(len(cases[i]['image'][0]) for i in expected_stages),
                                               max_height=max(len(cases[i]['image']) for i in expected_stages))
        report['compiled_shaders'] = {name: digest(retained / (name + '.cso')) for name in ('quad', *KERNELS)}
        report['sources_after'] = {p: digest(p) for p in before}
        report['runtime_files_after'] = {p: digest(p) for p in report['runtime_files_before']}
        if report['sources_after'] != before or digest(exe) != report['executable_sha256'] \
            or digest(bundle) != report['case_bundle_sha256'] or digest(compiler) != report['compiler_sha256_before'] \
            or report['runtime_files_before'] != report['runtime_files_after'] \
            or any(digest(p) != sha for p, sha in report['expanded_sources'].items()):
            raise RuntimeError('Inputs/runtime changed during fixture')
        if not all(image['passed'] for image in report['images']):
            raise RuntimeError('GPU readback differs from the predeclared oracle tolerance')
        report.update(passed=True, phase='complete', gpu_execution_verified=True,
                      channels=sum(image['channels'] for image in report['images']))
    except Exception as error:
        report.update(phase='failed', error=str(error))
        raise
    finally:
        # Preserve post-run identity also for rejected numerical/API runs.
        report['sources_after'] = {p: digest(p) for p in before}
        report['executable_sha256_after'] = digest(exe)
        report['compiler_sha256_after'] = digest(compiler)
        report['case_bundle_sha256_after'] = digest(bundle)
        report['characterization_bundle_sha256_after'] = digest(characterization_bundle)
        report['runtime_files_after'] = {p: digest(p) for p in report['runtime_files_before']}
        report['expanded_sources_after'] = {p: digest(p) for p in report['expanded_sources']}
        report['inputs_unchanged'] = (report['sources_after'] == before
            and report['executable_sha256_after'] == report['executable_sha256']
            and report['compiler_sha256_after'] == report['compiler_sha256_before']
            and report['case_bundle_sha256_after'] == report['case_bundle_sha256']
            and report['characterization_bundle_sha256_after'] == report['characterization_bundle_sha256']
            and report['runtime_files_after'] == report['runtime_files_before']
            and report['expanded_sources_after'] == report['expanded_sources'])
        if not report['inputs_unchanged']:
            report.update(passed=False, phase='failed', error='Inputs changed during the GPU run')
        write_report()
        summary.write_bytes(local_summary.read_bytes())
        print(json.dumps(dict(passed=report['passed'], phase=report['phase'], images=len(report['images']),
                              summary=str(summary), retained=str(retained))))
        if not report['inputs_unchanged']:
            raise RuntimeError('Inputs changed during the GPU run')
    if not report['passed']:
        raise RuntimeError(report.get('error', 'GPU verification failed'))


if __name__ == '__main__':
    main()
